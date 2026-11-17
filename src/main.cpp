// SPDX-License-Identifier: GPL-3.0-or-later
// Settle - Copyright (C) 2026 Robert. This program comes with ABSOLUTELY NO WARRANTY; see LICENSE.
// Settle - Seat Collision for SMP Cloth (SKSE plugin). Needs the Settle build of Faster HDT-SMP
// (hdtsmp64.dll with the world-collision interface, WorldCollisionAPI.h).
// - Seat collision: on TESFurnitureEvent enter (main thread via the SKSE task queue) the furniture's Havok
//   collision triangles are extracted in world space and registered with FSMP as a collider for that actor's
//   SMP bodies; removed again on exit.
// - Seated hug: short skirts get their lower cloth bones tied to the thighs/shins while seated (and locked after
//   fHugFreezeDelay); long skirts (fHugLongSkirt) keep plain physics + seat collision.
// - Per-outfit hug overrides: Data\SKSE\Plugins\Settle\HugOverrides\*.ini ([physics xml path] + fHug* keys).
// - Actors already seated when a save loads are picked up ~3 s after the load.
// - Seat swap: Data\SKSE\Plugins\Settle\SeatSwap\*.ini ("standing physics XML = seated physics XML").
// Settings: Data\SKSE\Plugins\Settle.ini, re-read on every sit. Log: Documents\My Games\Skyrim Special Edition\SKSE\Settle.log

#include "WorldCollisionAPI.h"

namespace
{
	constexpr float kHavokToGame = 69.99125f;

	struct V3
	{
		float x{}, y{}, z{};
		V3 operator+(const V3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		V3 operator-(const V3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		V3 operator*(float s) const { return { x * s, y * s, z * s }; }
	};

	V3 ToV3(const RE::hkVector4& a_v)
	{
		alignas(16) float f[4];
		_mm_store_ps(f, a_v.quad);
		return { f[0], f[1], f[2] };
	}

	std::string Fmt(const V3& a_v) { return std::format("({:.1f}, {:.1f}, {:.1f})", a_v.x, a_v.y, a_v.z); }

	RE::hkTransform Identity()
	{
		RE::hkTransform t;
		t.rotation.col0 = RE::hkVector4(1.0f, 0.0f, 0.0f, 0.0f);
		t.rotation.col1 = RE::hkVector4(0.0f, 1.0f, 0.0f, 0.0f);
		t.rotation.col2 = RE::hkVector4(0.0f, 0.0f, 1.0f, 0.0f);
		t.translation = RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f);
		return t;
	}

	// hkpTriangleShape is not in CommonLib. Havok 2010 layout: hkpConvexShape (0x28) +
	// u16 weldingInfo (0x28), u8 weldingType (0x2A), u8 isExtruded (0x2B), vertexA/B/C at 0x30/0x40/0x50.
	struct TriangleShapeView
	{
		static const RE::hkVector4* Vertices(const RE::hkpShape* a_shape)
		{
			return reinterpret_cast<const RE::hkVector4*>(reinterpret_cast<const std::byte*>(a_shape) + 0x30);
		}
	};

	struct Tri
	{
		V3 a, b, c;  // local to the rigid body, game units
	};

	struct BodyTris
	{
		std::string     node;
		RE::hkTransform bodyXf;    // Havok units
		RE::NiTransform nodeWorld;
		RE::hkAabb      worldAabb;  // Havok units
		std::vector<Tri> tris;
		int             otherShapes = 0;
		int             layoutBad = 0;
		int             hulls = 0;
		int             boxFallbacks = 0;
		std::vector<int> otherTypes;
		std::string     rtti;
		bool            hasT = false;      // bhkRigidBodyT: extra local transform between node and shape
		V3              tTrans{};          // game units
		float           tQuat[4]{ 0, 0, 0, 1 };
		RE::hkTransform motionXf;          // hkpRigidBody motion state (Havok units)
	};

	// Fallback for primitive shapes (box, capsule, sphere, ...): the shape's local AABB as 12 triangles.
	void BoxTris(const RE::hkpShape* a_shape, BodyTris& a_out)
	{
		RE::hkAabb box;
		a_shape->GetAabbImpl(Identity(), 0.0f, box);
		const V3 n = ToV3(box.min) * kHavokToGame, x = ToV3(box.max) * kHavokToGame;
		const V3 c[8] = { { n.x, n.y, n.z }, { x.x, n.y, n.z }, { x.x, x.y, n.z }, { n.x, x.y, n.z },
			{ n.x, n.y, x.z }, { x.x, n.y, x.z }, { x.x, x.y, x.z }, { n.x, x.y, x.z } };
		const int f[12][3] = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 },
			{ 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
		for (const auto& t : f) {
			a_out.tris.push_back({ c[t[0]], c[t[1]], c[t[2]] });
		}
		++a_out.boxFallbacks;
	}

	// Convex hull -> triangles: for each plane equation, take the vertices lying on it, sort them around
	// the plane normal (counter-clockwise, so triangle normals point outward) and fan-triangulate.
	bool HullTris(const RE::hkpConvexVerticesShape* a_cv, BodyTris& a_out)
	{
		std::vector<V3> verts;
		for (std::int32_t i = 0; i < a_cv->numVertices; ++i) {
			const auto& four = a_cv->rotatedVertices[i / 4];
			alignas(16) float xs[4], ys[4], zs[4];
			_mm_store_ps(xs, four.x.quad);
			_mm_store_ps(ys, four.y.quad);
			_mm_store_ps(zs, four.z.quad);
			verts.push_back({ xs[i % 4], ys[i % 4], zs[i % 4] });  // Havok units
		}
		if (verts.size() < 4 || a_cv->planeEquations.empty()) {
			return false;
		}
		std::size_t added = 0;
		for (const auto& pe : a_cv->planeEquations) {
			alignas(16) float q[4];
			_mm_store_ps(q, pe.quad);
			const V3 nrm{ q[0], q[1], q[2] };
			std::vector<V3> on;
			for (const auto& v : verts) {
				if (std::fabs(nrm.x * v.x + nrm.y * v.y + nrm.z * v.z + q[3]) < 1e-3f) {
					on.push_back(v);
				}
			}
			if (on.size() < 3) {
				continue;
			}
			V3 ctr{};
			for (const auto& v : on) {
				ctr = ctr + v;
			}
			ctr = ctr * (1.0f / static_cast<float>(on.size()));
			// in-plane basis u, w with u x w = n
			V3 u = on[0] - ctr;
			const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
			if (ul < 1e-6f) {
				continue;
			}
			u = u * (1.0f / ul);
			const V3 w{ nrm.y * u.z - nrm.z * u.y, nrm.z * u.x - nrm.x * u.z, nrm.x * u.y - nrm.y * u.x };
			std::sort(on.begin(), on.end(), [&](const V3& a, const V3& b) {
				const V3 da = a - ctr, db = b - ctr;
				return std::atan2(da.x * w.x + da.y * w.y + da.z * w.z, da.x * u.x + da.y * u.y + da.z * u.z) <
				       std::atan2(db.x * w.x + db.y * w.y + db.z * w.z, db.x * u.x + db.y * u.y + db.z * u.z);
			});
			for (std::size_t k = 1; k + 1 < on.size(); ++k) {
				a_out.tris.push_back({ on[0] * kHavokToGame, on[k] * kHavokToGame, on[k + 1] * kHavokToGame });
				++added;
			}
		}
		if (added) {
			++a_out.hulls;
		}
		return added > 0;
	}

	void CollectTris(const RE::hkpShape* a_shape, BodyTris& a_out, int a_depth = 0)
	{
		if (!a_shape || a_depth > 8) {
			return;
		}
		if (a_shape->type == RE::hkpShapeType::kTriangle) {
			const auto* v = TriangleShapeView::Vertices(a_shape);
			Tri t{ ToV3(v[0]) * kHavokToGame, ToV3(v[1]) * kHavokToGame, ToV3(v[2]) * kHavokToGame };

			// validate the assumed layout: every vertex must lie inside the shape's own local AABB
			RE::hkAabb box;
			a_shape->GetAabbImpl(Identity(), 0.0f, box);
			const V3 mn = ToV3(box.min) * kHavokToGame, mx = ToV3(box.max) * kHavokToGame;
			constexpr float eps = 1.0f;
			for (const V3* p : { &t.a, &t.b, &t.c }) {
				if (p->x < mn.x - eps || p->y < mn.y - eps || p->z < mn.z - eps ||
					p->x > mx.x + eps || p->y > mx.y + eps || p->z > mx.z + eps) {
					++a_out.layoutBad;
					break;
				}
			}
			a_out.tris.push_back(t);
			return;
		}
		if (const auto* container = a_shape->GetContainer()) {
			for (auto key = container->GetFirstKey(); key != static_cast<RE::hkpShapeKey>(-1); key = container->GetNextKey(key)) {
				RE::hkpShapeBuffer buffer;
				CollectTris(container->GetChildShape(key, buffer), a_out, a_depth + 1);
			}
			return;
		}
		++a_out.otherShapes;
		a_out.otherTypes.push_back(static_cast<int>(a_shape->type));
		if (a_shape->type == RE::hkpShapeType::kConvexVertices && HullTris(static_cast<const RE::hkpConvexVerticesShape*>(a_shape), a_out)) {
			return;
		}
		BoxTris(a_shape, a_out);
	}

	void CollectBodies(RE::NiAVObject* a_obj, std::vector<BodyTris>& a_out)
	{
		if (!a_obj) {
			return;
		}
		if (a_obj->collisionObject) {
			auto* bhkCol = a_obj->collisionObject->AsBhkNiCollisionObject();
			auto* worldObj = bhkCol ? bhkCol->body.get() : nullptr;
			if (auto* rigid = worldObj ? netimmerse_cast<RE::bhkRigidBody*>(worldObj) : nullptr) {
				BodyTris b;
				b.node = a_obj->name.c_str();
				rigid->GetTransform(b.bodyXf);
				b.rtti = worldObj->GetRTTI() ? worldObj->GetRTTI()->GetName() : "?";
				if (b.rtti == "bhkRigidBodyT") {
					// bhkRigidBodyT privately inherits bhkRigidBody in CommonLib; read its members by layout (0x40 quat, 0x50 translation)
					const auto* base = reinterpret_cast<const std::byte*>(worldObj);
					alignas(16) float q[4], t[4];
					_mm_store_ps(q, reinterpret_cast<const RE::hkVector4*>(base + 0x40)->quad);
					_mm_store_ps(t, reinterpret_cast<const RE::hkVector4*>(base + 0x50)->quad);
					b.hasT = true;
					for (int k = 0; k < 4; ++k) {
						b.tQuat[k] = q[k];
					}
					b.tTrans = V3{ t[0], t[1], t[2] } * kHavokToGame;
				}
				if (auto* hk0 = rigid->GetRigidBody()) {
					b.motionXf = hk0->motion.motionState.transform;
				}
				rigid->GetAabbWorldspace(b.worldAabb);
				b.nodeWorld = a_obj->world;
				if (auto* hk = rigid->GetRigidBody()) {
					CollectTris(hk->GetShape(), b);
				}
				a_out.push_back(std::move(b));
			}
		}
		if (auto* node = a_obj->AsNode()) {
			for (auto& child : node->GetChildren()) {
				CollectBodies(child.get(), a_out);
			}
		}
	}

	// Havok body transform (Havok units) applied to a local point in game units -> world game units
	V3 ByBody(const RE::hkTransform& a_xf, const V3& p)
	{
		const V3 c0 = ToV3(a_xf.rotation.col0), c1 = ToV3(a_xf.rotation.col1), c2 = ToV3(a_xf.rotation.col2);
		return c0 * p.x + c1 * p.y + c2 * p.z + ToV3(a_xf.translation) * kHavokToGame;
	}

	// NiNode world transform applied to a local point in game units
	V3 ByNode(const RE::NiTransform& a_xf, const V3& p)
	{
		const RE::NiPoint3 w = a_xf * RE::NiPoint3(p.x, p.y, p.z);
		return { w.x, w.y, w.z };
	}

	V3 QuatRotate(const float q[4], const V3& v)
	{
		// v' = v + 2w(u x v) + 2 u x (u x v), u = (x, y, z)
		const V3 u{ q[0], q[1], q[2] };
		const float w = q[3];
		const V3 uv{ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
		const V3 uuv{ u.y * uv.z - u.z * uv.y, u.z * uv.x - u.x * uv.z, u.x * uv.y - u.y * uv.x };
		return v + uv * (2.0f * w) + uuv * 2.0f;
	}

	// best world position: node world transform, plus the bhkRigidBodyT local transform when present
	V3 World(const BodyTris& b, const V3& p)
	{
		if (!b.hasT) {
			return ByBody(b.bodyXf, p);
		}
		return ByNode(b.nodeWorld, QuatRotate(b.tQuat, p) + b.tTrans);
	}

	// highest collision surface directly below (x, y) and not above maxZ; returns false if none
	bool SurfaceBelow(const std::vector<BodyTris>& a_bodies, float x, float y, float maxZ, float& outZ)
	{
		bool found = false;
		for (const auto& b : a_bodies) {
			for (const auto& t : b.tris) {
				const V3 A = World(b, t.a), B = World(b, t.b), C = World(b, t.c);
				const float d = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
				if (std::fabs(d) < 1e-4f) {
					continue;  // vertical triangle
				}
				const float l1 = ((B.y - C.y) * (x - C.x) + (C.x - B.x) * (y - C.y)) / d;
				const float l2 = ((C.y - A.y) * (x - C.x) + (A.x - C.x) * (y - C.y)) / d;
				const float l3 = 1.0f - l1 - l2;
				if (l1 < 0 || l2 < 0 || l3 < 0) {
					continue;
				}
				const float z = l1 * A.z + l2 * B.z + l3 * C.z;
				if (z <= maxZ && (!found || z > outZ)) {
					outZ = z;
					found = true;
				}
			}
		}
		return found;
	}

	std::string SafeFileName(std::string s)
	{
		for (auto& ch : s) {
			if (!std::isalnum(static_cast<unsigned char>(ch))) {
				ch = '_';
			}
		}
		return s;
	}

	void WriteObj(const std::vector<BodyTris>& a_bodies, const std::string& a_name)
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		const auto folder = *dir / "Settle";
		std::error_code ec;
		std::filesystem::create_directories(folder, ec);
		const auto path = folder / (SafeFileName(a_name) + ".obj");
		std::ofstream f(path);
		f << "# Settle furniture collision, world space, game units\n";
		std::size_t base = 1;
		for (const auto& b : a_bodies) {
			f << "o " << SafeFileName(b.node) << "\n";
			for (const auto& t : b.tris) {
				for (const V3& p : { t.a, t.b, t.c }) {
					const V3 w = World(b, p);
					f << std::format("v {:.3f} {:.3f} {:.3f}\n", w.x, w.y, w.z);
				}
				f << std::format("f {} {} {}\n", base, base + 1, base + 2);
				base += 3;
			}
		}
		SKSE::log::info("    wrote {}", path.string());
	}


	// ------------------------------------------------------------------ settings

	// Release defaults (tuned in game 2026-11-16): rigid hug of short skirts, cloth resting on the thigh surface,
	// back panels lifted, shins tied below kneeT, long skirts skipped.
	hdt::world::HugParams DefaultHug()
	{
		hdt::world::HugParams h;
		h.tMin = -0.05f;
		h.tMax = 1.15f;
		h.maxDist = 35.0f;
		h.linearLimit = 0.0f;
		h.angularLimit = 0.0f;
		h.linearStiffness = 20.0f;
		h.angularStiffness = 5.0f;
		h.damping = 0.5f;
		h.backLift = 3.5f;
		h.pull = 30.0f;
		h.surface = 5.5f;
		h.debug = 0;
		h.shinMax = 1.0f;
		h.shinSurface = 5.0f;
		h.pullFraction = 1.0f;
		h.kneeT = 0.9f;
		h.centerBand = 0.0f;
		h.pullMode = 0.0f;
		h.frontArc = 0.0f;
		h.centerBlend = 0.0f;
		h.longSkirt = 0.18f;
		return h;
	}

	struct Settings
	{
		bool  enabled = true;
		bool  playerOnly = false;
		float maxDistance = 4096.0f;   // actors farther than this from the player are ignored
		bool  upFacingOnly = true;     // keep only seat-like triangles (normal pointing up)
		float upFacingMinZ = 0.5f;     // normal.z threshold for "up facing" (0.5 = up to 60 degrees tilt)
		float margin = 1.0f;
		float penetration = 12.0f;     // up-facing mode: cloth this far below a seat surface is pushed up onto it
		float friction = 0.5f;
		bool  debugObj = false;        // player only: write the registered triangles as .obj
		bool  seatSwap = true;         // use the SeatSwap pairs (needs the fork's interface v2)
		bool  autoHug = true;          // generic seated hug (needs interface v3)
		hdt::world::HugParams hug = DefaultHug();  // release defaults (Settle.ini overrides any of them)
		float freezeDelay = 2.5f;
		float seatAbove = 3.0f;        // up-facing mode: drop surfaces whose lowest point is more than this above the seat (armrests); < 0 = keep all      // seconds after the actor is fully seated before the hug is locked (0 = never)
	};

	std::string Trim(std::string s)
	{
		const auto ws = " \t\r\n";
		s.erase(0, s.find_first_not_of(ws));
		const auto e = s.find_last_not_of(ws);
		s.erase(e == std::string::npos ? 0 : e + 1);
		return s;
	}

	// fHug* keys (main INI [AutoHug] and HugOverrides sections); key already lower-case
	bool ApplyHugKey(hdt::world::HugParams& h, const std::string& key, float v)
	{
		if (key == "fhugmint") h.tMin = v;
		else if (key == "fhugmaxt") h.tMax = v;
		else if (key == "fhugmaxdist") h.maxDist = v;
		else if (key == "fhuglinearlimit") h.linearLimit = v;
		else if (key == "fhugangularlimit") h.angularLimit = v;
		else if (key == "fhuglinearstiffness") h.linearStiffness = v;
		else if (key == "fhugangularstiffness") h.angularStiffness = v;
		else if (key == "fhugdamping") h.damping = v;
		else if (key == "fhugbacklift") h.backLift = v;
		else if (key == "fhugpull") h.pull = v;
		else if (key == "fhugsurface") h.surface = v;
		else if (key == "bhugdebug") h.debug = v != 0.0f ? 1u : 0u;
		else if (key == "fhugshinmax") h.shinMax = v;
		else if (key == "fhugshinsurface") h.shinSurface = v;
		else if (key == "fhugpullfraction") h.pullFraction = v;
		else if (key == "fhugkneet") h.kneeT = v;
		else if (key == "fhugcenterband") h.centerBand = v;
		else if (key == "fhugpullmode") h.pullMode = v;
		else if (key == "fhugfrontarc") h.frontArc = v;
		else if (key == "fhugcenterblend") h.centerBlend = v;
		else if (key == "fhuglongskirt") h.longSkirt = v;
		else return false;
		return true;
	}

	Settings LoadSettings()
	{
		Settings s;
		std::ifstream f("Data/SKSE/Plugins/Settle.ini");
		if (!f) {
			return s;
		}
		std::string line;
		while (std::getline(f, line)) {
			const auto c = line.find_first_of(";#");
			if (c != std::string::npos) {
				line.erase(c);
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (key.empty() || val.empty()) {
				continue;
			}
			try {
				const float v = std::stof(val);
				if (key == "benabled") s.enabled = v != 0.0f;
				else if (key == "bplayeronly") s.playerOnly = v != 0.0f;
				else if (key == "fmaxdistance") s.maxDistance = v;
				else if (key == "bupfacingonly") s.upFacingOnly = v != 0.0f;
				else if (key == "fupfacingminz") s.upFacingMinZ = v;
				else if (key == "fmargin") s.margin = v;
				else if (key == "fpenetration") s.penetration = v;
				else if (key == "ffriction") s.friction = v;
				else if (key == "bdebugobj") s.debugObj = v != 0.0f;
				else if (key == "bseatswap") s.seatSwap = v != 0.0f;
				else if (key == "bautohug") s.autoHug = v != 0.0f;
				else if (key == "fhugfreezedelay") s.freezeDelay = v;
				else if (key == "fseatabove") s.seatAbove = v;
				else ApplyHugKey(s.hug, key, v);
			} catch (...) {
			}
		}
		return s;
	}


	struct SwapPair
	{
		std::string standing, seated;
	};

	// Data\SKSE\Plugins\Settle\SeatSwap\*.ini: "standing XML path = seated XML path" per line.
	// Paths must match the physics path exactly as the outfit's NIF writes it (e.g. meshes\...\x.xml).
	std::vector<SwapPair> LoadSwapPairs()
	{
		std::vector<SwapPair> pairs;
		std::error_code ec;
		const std::filesystem::path dir("Data/SKSE/Plugins/Settle/SeatSwap");
		if (!std::filesystem::is_directory(dir, ec)) {
			return pairs;
		}
		for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
			if (!e.is_regular_file(ec)) {
				continue;
			}
			auto ext = e.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (ext != ".ini") {
				continue;
			}
			std::ifstream f(e.path());
			std::string line;
			while (std::getline(f, line)) {
				const auto c = line.find(';');
				if (c != std::string::npos) {
					line.erase(c);
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}
				SwapPair sp{ Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)) };
				if (!sp.standing.empty() && !sp.seated.empty() && sp.standing != sp.seated) {
					pairs.push_back(std::move(sp));
				}
			}
		}
		return pairs;
	}

	// Data\SKSE\Plugins\Settle\HugOverrides\*.ini:
	//   [meshes\armor\Some Outfit\skirt.xml]   <- the outfit's physics XML path (as in the log's "hug ..." lines)
	//   fHugPullFraction = 0.5                  <- any fHug* key; unlisted keys keep the main INI value
	std::vector<std::pair<std::string, hdt::world::HugParams>> LoadHugOverrides(const hdt::world::HugParams& base)
	{
		std::vector<std::pair<std::string, hdt::world::HugParams>> out;
		std::error_code ec;
		const std::filesystem::path dir("Data/SKSE/Plugins/Settle/HugOverrides");
		if (!std::filesystem::is_directory(dir, ec)) {
			return out;
		}
		for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
			auto ext = e.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (!e.is_regular_file(ec) || ext != ".ini") {
				continue;
			}
			std::ifstream f(e.path());
			std::string line;
			int cur = -1;
			while (std::getline(f, line)) {
				const auto c = line.find(';');
				if (c != std::string::npos) {
					line.erase(c);
				}
				line = Trim(line);
				if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
					out.emplace_back(Trim(line.substr(1, line.size() - 2)), base);
					cur = static_cast<int>(out.size()) - 1;
					continue;
				}
				const auto eq = line.find('=');
				if (cur < 0 || eq == std::string::npos) {
					continue;
				}
				std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
				std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
				try {
					ApplyHugKey(out[cur].second, key, std::stof(val));
				} catch (...) {
				}
			}
		}
		return out;
	}

	// ------------------------------------------------------------------ FSMP link

	hdt::world::IWorldCollision* g_world = nullptr;

	struct Active
	{
		hdt::world::Handle handle = 0;
		RE::FormID         furniture = 0;
		RE::ActorHandle    actor;
		std::vector<SwapPair> swapped;  // seat swaps applied on sit, reverted on stand
		bool               hug = false;  // generic hug turned on for this actor
		std::uint32_t      serial = 0;   // identifies this sit (for the delayed freeze)
	};
	std::uint32_t g_sitSerial = 0;
	std::unordered_map<RE::FormID, Active> g_active;  // by actor FormID; main thread only

	void RemoveFor(RE::FormID a_actor, const char* a_why)
	{
		auto it = g_active.find(a_actor);
		if (it == g_active.end()) {
			return;
		}
		if (it->second.handle) {
			const bool ok = g_world && g_world->removeCollider(it->second.handle);
			SKSE::log::info("--- {:08X}: seat collider {} removed ({}{})", a_actor, it->second.handle, a_why, ok ? "" : ", was already gone");
		}
		if (it->second.hug && g_world && g_world->version() >= 3) {
			g_world->setSeatedHug(a_actor, nullptr, nullptr, 0);
			SKSE::log::info("--- {:08X}: seated hug off ({})", a_actor, a_why);
		}
		for (const auto& sp : it->second.swapped) {
			const bool back = g_world && g_world->version() >= 2 && g_world->swapPhysicsFile(a_actor, sp.seated.c_str(), sp.standing.c_str());
			SKSE::log::info("--- {:08X}: seat swap reverted '{}' -> '{}': {}", a_actor, sp.seated, sp.standing, back ? "ok" : "failed (outfit changed or actor unloaded)");
		}
		g_active.erase(it);
	}

	void RemoveFor(RE::FormID a_actor, const char* a_why);

	void RemoveAll(const char* a_why)
	{
		std::vector<RE::FormID> ids;
		for (auto& [id, a] : g_active) {
			ids.push_back(id);
		}
		for (auto id : ids) {
			RemoveFor(id, a_why);  // hug off + swaps back while the systems still exist
		}
		if (g_world) {
			g_world->removeAll();
		}
		if (!g_active.empty()) {
			SKSE::log::info("--- all {} seat colliders removed ({})", g_active.size(), a_why);
		}
		g_active.clear();
	}

	void HugLog(const char* a_line)
	{
		SKSE::log::info("    {}", a_line);
	}

	// Safety net: drop colliders whose actor is gone or no longer sitting (missed exit events, unloads).
	void Sweep()
	{
		std::vector<RE::FormID> stale;
		for (auto& [id, a] : g_active) {
			auto actor = a.actor.get();
			const auto state = actor ? actor->AsActorState()->GetSitSleepState() : RE::SIT_SLEEP_STATE::kNormal;
			if (!actor || !actor->Get3D() || state == RE::SIT_SLEEP_STATE::kNormal) {
				stale.push_back(id);
			}
		}
		for (auto id : stale) {
			RemoveFor(id, "sweep: actor gone or not sitting");
		}
	}

	// world-space triangle soup -> welded vertex/index buffers
	struct Mesh
	{
		std::vector<float>         pos;
		std::vector<std::uint32_t> idx;
		std::uint32_t              dropped = 0;
		std::uint32_t              aboveSeat = 0;   // up-facing triangles dropped because they lie above the seat (armrests, backrest tops)
		float                      seatTop = 0.0f;  // world z of the detected seat surface (0 = not detected)
	};

	Mesh BuildMesh(const std::vector<BodyTris>& a_bodies, const Settings& s)
	{
		Mesh m;
		std::unordered_map<std::uint64_t, std::uint32_t> weld;
		auto vert = [&](const V3& p) -> std::uint32_t {
			const auto q = [](float f) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(f * 20.0f)) & 0x1FFFFF); };
			const std::uint64_t key = q(p.x) | (q(p.y) << 21) | (q(p.z) << 42);
			auto [it, fresh] = weld.try_emplace(key, static_cast<std::uint32_t>(m.pos.size() / 3));
			if (fresh) {
				m.pos.insert(m.pos.end(), { p.x, p.y, p.z });
			}
			return it->second;
		};
		// candidate triangles (world space, wound up when up-facing only)
		struct Tri
		{
			V3    A, B, C;
			bool  up;    // the original winding faced up (a real top surface)
			float area, zMin, zMax, zMid;
		};
		std::vector<Tri> tris;
		for (const auto& b : a_bodies) {
			for (const auto& t : b.tris) {
				V3 A = World(b, t.a), B = World(b, t.b), C = World(b, t.c);
				const V3 e1 = B - A, e2 = C - A;
				V3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
				const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
				if (len < 1e-4f) {
					++m.dropped;
					continue;
				}
				bool up = n.z > 0;
				if (s.upFacingOnly) {
					// orientation-independent: keep near-horizontal triangles, wound so the normal points up
					if (std::fabs(n.z) / len < s.upFacingMinZ) {
						++m.dropped;
						continue;
					}
					if (n.z < 0) {
						std::swap(B, C);
					}
				}
				const float zMin = std::min({ A.z, B.z, C.z }), zMax = std::max({ A.z, B.z, C.z });
				tris.push_back({ A, B, C, up, 0.5f * len, zMin, zMax, (A.z + B.z + C.z) / 3.0f });
			}
		}

		// Seat detection (up-facing mode): the seat is the largest horizontal level among the real top surfaces.
		// Surfaces entirely above it (armrests, backrest tops) would push the cloth beside the thighs up over them,
		// so they are dropped. Lower surfaces (stretchers, floor plates) are kept: cloth never reaches them from above.
		if (s.upFacingOnly && s.seatAbove >= 0.0f) {
			std::vector<const Tri*> ups;
			for (const auto& t : tris) {
				if (t.up) {
					ups.push_back(&t);
				}
			}
			std::sort(ups.begin(), ups.end(), [](const Tri* x, const Tri* y) { return x->zMid < y->zMid; });
			float bestArea = 0.0f, curArea = 0.0f, curTop = -1e30f, prevZ = -1e30f;
			for (const auto* t : ups) {
				if (t->zMid - prevZ > 3.0f) {  // a new level starts
					curArea = 0.0f;
					curTop = -1e30f;
				}
				prevZ = t->zMid;
				curArea += t->area;
				curTop = std::max(curTop, t->zMax);
				if (curArea > bestArea) {
					bestArea = curArea;
					m.seatTop = curTop;
				}
			}
			if (bestArea > 0.0f) {
				std::erase_if(tris, [&](const Tri& t) {
					const bool above = t.zMin > m.seatTop + s.seatAbove;
					if (above) {
						++m.aboveSeat;
					}
					return above;
				});
			}
		}

		for (const auto& t : tris) {
			{
				const V3 &A = t.A, &B = t.B, &C = t.C;
				const auto ia = vert(A), ib = vert(B), ic = vert(C);
				if (ia == ib || ib == ic || ia == ic) {
					++m.dropped;
					continue;
				}
				m.idx.insert(m.idx.end(), { ia, ib, ic });
			}
		}
		return m;
	}

	void OnSit(RE::Actor* a_actor, RE::TESObjectREFR* a_furn)
	{
		Sweep();
		const auto s = LoadSettings();
		const auto id = a_actor->GetFormID();
		RemoveFor(id, "re-sit");
		if (!s.enabled || !g_world) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		const bool isPlayer = a_actor == player;
		if (s.playerOnly && !isPlayer) {
			return;
		}
		if (!isPlayer && player && a_actor->GetPosition().GetDistance(player->GetPosition()) > s.maxDistance) {
			return;
		}
		auto* root = a_furn->Get3D();
		if (!root) {
			return;
		}

		// seated physics swap (before the collider; swapped systems keep their pose)
		std::vector<SwapPair> swapped;
		if (s.seatSwap && g_world->version() >= 2) {
			for (const auto& sp : LoadSwapPairs()) {
				if (g_world->swapPhysicsFile(id, sp.standing.c_str(), sp.seated.c_str())) {
					SKSE::log::info("+++ {} ({:08X}): seat swap '{}' -> '{}'", a_actor->GetName(), id, sp.standing, sp.seated);
					swapped.push_back(sp);
				}
			}
		}
		// generic hug for every item not already swapped to a hand-made seated XML
		bool hugOn = false;
		if (s.autoHug && g_world->version() >= 3) {
			std::vector<const char*> skip;
			for (const auto& sp : swapped) {
				skip.push_back(sp.seated.c_str());
			}
			auto params = s.hug;
			params.size = sizeof(params);
			params.log = &HugLog;
			if (g_world->version() >= 5) {
				g_world->clearHugOverrides();
				for (auto& [path, ov] : LoadHugOverrides(s.hug)) {
					ov.size = sizeof(ov);
					g_world->setHugOverride(path.c_str(), &ov);
					SKSE::log::info("    hug override '{}': pullFraction {:.2f}, pull {:.1f}, centerBand {:.1f}, kneeT {:.2f}, shinMax {:.2f}, pullMode {:.0f}, shinSurface {:.1f}, frontArc {:.0f}, centerBlend {:.1f}",
						path, ov.pullFraction, ov.pull, ov.centerBand, ov.kneeT, ov.shinMax, ov.pullMode, ov.shinSurface, ov.frontArc, ov.centerBlend);
				}
			}
			const auto n = g_world->setSeatedHug(id, &params, skip.data(), static_cast<std::uint32_t>(skip.size()));
			hugOn = true;
			SKSE::log::info("+++ {} ({:08X}): seated hug on, {} constraints (tMin {:.2f}, tMax {:.2f}, maxDist {:.1f}, lin {:.1f}/{:.0f}, ang {:.2f}/{:.0f}, damping {:.2f}, backLift {:.1f}, pull {:.1f}, surface {:.1f}, shinMax {:.2f}, shinSurface {:.1f}, pullFraction {:.2f}, kneeT {:.2f}, centerBand {:.1f})",
				a_actor->GetName(), id, n, params.tMin, params.tMax, params.maxDist, params.linearLimit, params.linearStiffness,
				params.angularLimit, params.angularStiffness, params.damping, params.backLift, params.pull, params.surface,
				params.shinMax, params.shinSurface, params.pullFraction, params.kneeT, params.centerBand);
		}
		const std::uint32_t serial = ++g_sitSerial;
		auto remember = [&](hdt::world::Handle h) {
			if (h || !swapped.empty() || hugOn) {
				g_active[id] = { h, a_furn->GetFormID(), a_actor->GetHandle(), std::move(swapped), hugOn, serial };
			}
		};
		// freeze the settled cloth once the actor is fully seated (+ freezeDelay)
		if (hugOn && s.freezeDelay > 0.0f && g_world->version() >= 4) {
			auto actorHandle = a_actor->GetHandle();
			const float delay = s.freezeDelay;
			std::thread([actorHandle, id, serial, delay]() {
				auto seatedSince = std::chrono::steady_clock::time_point{};
				for (int i = 0; i < 160; ++i) {  // up to 40 s
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
					auto state = std::make_shared<int>(0);  // 0 waiting, 1 seated, 2 abort
					auto done = std::make_shared<std::atomic<bool>>(false);
					SKSE::GetTaskInterface()->AddTask([actorHandle, id, serial, state, done]() {
						auto it = g_active.find(id);
						auto actor = actorHandle.get();
						if (it == g_active.end() || it->second.serial != serial || !actor) {
							*state = 2;
						} else if (actor->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSitting) {
							*state = 1;
						}
						done->store(true);
					});
					for (int w = 0; w < 200 && !done->load(); ++w) {
						std::this_thread::sleep_for(std::chrono::milliseconds(5));
					}
					if (!done->load() || *state == 2) {
						return;
					}
					if (*state == 1) {
						if (seatedSince == std::chrono::steady_clock::time_point{}) {
							seatedSince = std::chrono::steady_clock::now();
						}
						if (std::chrono::steady_clock::now() - seatedSince >= std::chrono::milliseconds(static_cast<int>(delay * 1000))) {
							SKSE::GetTaskInterface()->AddTask([id, serial]() {
								auto it = g_active.find(id);
								if (it == g_active.end() || it->second.serial != serial || !g_world) {
									return;
								}
								const auto n = g_world->freezeSeatedHug(id);
								SKSE::log::info("+++ {:08X}: seated hug frozen ({} constraints locked)", id, n);
							});
							return;
						}
					}
				}
			}).detach();
		}

		std::vector<BodyTris> bodies;
		CollectBodies(root, bodies);
		std::size_t raw = 0;
		for (const auto& b : bodies) {
			raw += b.tris.size();
		}
		auto* base = a_furn->GetBaseObject();
		const std::string furnName = base ? base->GetName() : "";
		if (raw == 0) {
			SKSE::log::info("+++ {} ({:08X}) sat in {:08X} '{}': no collision (marker furniture), no collider", a_actor->GetName(), id, a_furn->GetFormID(), furnName);
			remember(0);
			return;
		}
		const auto mesh = BuildMesh(bodies, s);
		if (mesh.idx.empty()) {
			SKSE::log::info("+++ {} ({:08X}) sat in {:08X} '{}': {} triangles, none kept (filter), no collider", a_actor->GetName(), id, a_furn->GetFormID(), furnName, raw);
			remember(0);
			return;
		}
		hdt::world::ColliderDesc d;
		d.positions = mesh.pos.data();
		d.vertexCount = static_cast<std::uint32_t>(mesh.pos.size() / 3);
		d.indices = mesh.idx.data();
		d.triangleCount = static_cast<std::uint32_t>(mesh.idx.size() / 3);
		d.margin = s.margin;
		d.penetration = s.upFacingOnly ? s.penetration : 0.0f;  // all-triangles mode: winding unknown -> two-sided
		d.friction = s.friction;
		d.ownerFormID = id;
		const auto handle = g_world->addCollider(d);
		SKSE::log::info("+++ {} ({:08X}) sat in {:08X} '{}': {} of {} triangles, {} vertices -> collider {} (margin {:.1f}, penetration {:.1f}, friction {:.2f}, {})",
			a_actor->GetName(), id, a_furn->GetFormID(), furnName, d.triangleCount, raw, d.vertexCount, handle,
			d.margin, d.penetration, d.friction, s.upFacingOnly ? "up-facing only" : "all triangles");
		if (mesh.aboveSeat > 0) {
			SKSE::log::info("    seat at z {:.1f}: {} raised surfaces (armrests, backrest tops) left out", mesh.seatTop, mesh.aboveSeat);
		}
		remember(handle);
		if (isPlayer && s.debugObj) {
			WriteObj(bodies, std::format("{:08X}_{}", a_furn->GetFormID(), furnName.empty() ? "furniture" : furnName));
		}
	}

	// Actors that are already seated when a save finishes loading get no TESFurnitureEvent, so their seat
	// collision / hug would only appear after they stand up and sit again. Re-apply for them once the load
	// has settled (3D and SMP systems built).
	void RescanSeated(const char* a_why)
	{
		std::thread([why = std::string(a_why)]() {
			std::this_thread::sleep_for(std::chrono::seconds(3));
			SKSE::GetTaskInterface()->AddTask([why]() {
				std::vector<RE::Actor*> actors;
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					actors.push_back(player);
				}
				if (auto* lists = RE::ProcessLists::GetSingleton()) {
					for (auto& h : lists->highActorHandles) {
						if (auto a = h.get()) {
							actors.push_back(a.get());
						}
					}
				}
				int n = 0;
				for (auto* a : actors) {
					auto* state = a ? a->AsActorState() : nullptr;
					if (!state || state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kIsSitting) {
						continue;
					}
					auto furn = a->GetOccupiedFurniture().get();
					if (!furn) {
						continue;
					}
					SKSE::log::info("+++ {} ({:08X}) already seated after {}: applying", a->GetName(), a->GetFormID(), why);
					OnSit(a, furn.get());
					++n;
				}
				SKSE::log::info("rescan after {}: {} seated actor(s)", why, n);
			});
		}).detach();
	}

	class FurnitureSink final : public RE::BSTEventSink<RE::TESFurnitureEvent>
	{
	public:
		static FurnitureSink* Get()
		{
			static FurnitureSink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESFurnitureEvent* a_event, RE::BSTEventSource<RE::TESFurnitureEvent>*) override
		{
			if (!a_event || !a_event->actor || !a_event->targetFurniture) {
				return RE::BSEventNotifyControl::kContinue;
			}
			// events arrive on several threads: do the work on the main thread
			const bool enter = a_event->type == RE::TESFurnitureEvent::FurnitureEventType::kEnter;
			auto actorHandle = a_event->actor->GetHandle();
			auto furnHandle = a_event->targetFurniture->GetHandle();
			const auto actorID = a_event->actor->GetFormID();
			SKSE::GetTaskInterface()->AddTask([enter, actorHandle, furnHandle, actorID]() {
				if (!enter) {
					RemoveFor(actorID, "stood up");
					return;
				}
				auto actor = actorHandle.get();
				auto furn = furnHandle.get();
				auto* asActor = actor ? actor->As<RE::Actor>() : nullptr;
				if (asActor && furn) {
					OnSit(asActor, furn.get());
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void OnFsmpMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != hdt::world::MSG_WORLD_COLLISION_INTERFACE || !a_msg->data) {
			return;
		}
		auto* api = static_cast<hdt::world::IWorldCollision*>(a_msg->data);
		if (api->version() < 1) {
			SKSE::log::error("FSMP world-collision interface v{} is not supported", api->version());
			return;
		}
		g_world = api;
		SKSE::log::info("FSMP world-collision interface v{} received{}{}", api->version(), api->version() >= 2 ? "" : " (no seat swap: needs v2)",
			api->version() >= 3 ? "" : " (no generic hug: needs v3)");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			if (!SKSE::GetMessagingInterface()->RegisterListener("hdtsmp64", OnFsmpMessage)) {
				SKSE::log::warn("could not listen to hdtsmp64 (is Faster HDT-SMP installed?)");
			}
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			if (!g_world) {
				SKSE::log::error("no FSMP world-collision interface: this needs the Flobrim fork of Faster HDT-SMP. Seat collision is OFF.");
			}
			RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESFurnitureEvent>(FurnitureSink::Get());
			{
				const auto s = LoadSettings();
				SKSE::log::info("kDataLoaded: furniture sink registered; settings: enabled {}, playerOnly {}, maxDistance {:.0f}, upFacingOnly {} (minZ {:.2f}), margin {:.1f}, penetration {:.1f}, friction {:.2f}, seatSwap {}",
					s.enabled, s.playerOnly, s.maxDistance, s.upFacingOnly, s.upFacingMinZ, s.margin, s.penetration, s.friction, s.seatSwap);
				SKSE::log::info("  auto hug {}: tMin {:.2f}, tMax {:.2f}, maxDist {:.1f}, linearLimit {:.1f}, angularLimit {:.2f}, stiffness {:.0f}/{:.0f}, damping {:.2f}, backLift {:.1f}",
					s.autoHug, s.hug.tMin, s.hug.tMax, s.hug.maxDist, s.hug.linearLimit, s.hug.angularLimit, s.hug.linearStiffness, s.hug.angularStiffness, s.hug.damping, s.hug.backLift);
				for (const auto& sp : LoadSwapPairs()) {
					SKSE::log::info("  seat swap pair: '{}' -> '{}'", sp.standing, sp.seated);
				}
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			RemoveAll("loading a save");
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			if (g_world) {
				RescanSeated("loading a save");
			}
			break;
		case SKSE::MessagingInterface::kNewGame:
			RemoveAll("new game");
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::info("Settle {} loaded; game runtime {}",
		SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."),
		REL::Module::get().version().string("."));

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		SKSE::log::critical("could not register the SKSE message listener");
		return false;
	}
	return true;
}
