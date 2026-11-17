Settle - seat swap files

Optional hand-tuned seated physics for specific outfits.
Any *.ini file in this folder lists pairs, one per line:

  standing physics XML = seated physics XML

e.g.  meshes\armor\SomeAuthor\Robe.xml = meshes\armor\SomeAuthor\Robe_Seated.xml

The left path must match the path written in the outfit's NIF exactly (Settle.log shows it in its "hug ..." lines).
While an actor wearing that outfit sits, the outfit uses the seated XML; it switches back when they stand.
Outfits with a seat swap pair are not touched by the automatic hug.
Turn the feature off with bSeatSwap = 0 in Settle.ini.
