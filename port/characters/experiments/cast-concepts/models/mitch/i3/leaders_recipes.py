"""Identity recipes traced from the generated leader sheets and original portraits."""
GRAY=(.30,.34,.40);DARK=(.12,.11,.14);WHITE=(.94,.93,.90)
RECIPES={
'ishihara':dict(hair=(.24,.16,.11),top=(.62,.39,.86),sleeve=WHITE,pants=GRAY,style='vest',haircut='recede',eyes='firm'),
'imakuni':dict(hair=(.09,.085,.10),top=(.09,.085,.10),sleeve=(.09,.085,.10),pants=(.09,.085,.10),style='hood',haircut='hood',eyes='surprised',long=True),
'mitch':dict(hair=(.28,.16,.10),top=(.22,.68,.91),sleeve=(.22,.68,.91),pants=GRAY,style='open',haircut='spike',eyes='competitive',wide=1.05),
'gene':dict(hair=(.96,.28,.51),top=(.94,.22,.48),sleeve=(.94,.22,.48),pants=GRAY,style='jacket',haircut='sweep',eyes='competitive',long=True),
'isaac':dict(hair=(.97,.88,.23),top=WHITE,sleeve=WHITE,pants=GRAY,style='coat',haircut='sweep',eyes='glasses',shirt=(.98,.83,.12)),
'nikki':dict(hair=(.20,.115,.075),top=(.20,.79,.15),sleeve=(.20,.79,.15),pants=(.90,.68,.40),style='dress',haircut='bob',eyes='gentle',bare=True),
'murray':dict(hair=(.23,.135,.09),top=(.96,.53,.70),sleeve=WHITE,pants=GRAY,style='vest',haircut='longrecede',eyes='glasses',wide=1.10),
'rick':dict(hair=(.22,.13,.09),top=WHITE,sleeve=WHITE,pants=GRAY,style='coat',haircut='sweep',eyes='glasses',shirt=(.25,.57,.90)),
'ken':dict(hair=(.20,.115,.08),top=(.94,.22,.48),sleeve=(.94,.22,.48),pants=GRAY,style='jacket',haircut='part',eyes='competitive',long=True),
'courtney':dict(hair=(.96,.49,.70),top=(.96,.56,.73),sleeve=(.96,.56,.73),pants=(.90,.68,.40),style='gown',haircut='longbob',eyes='gentle',bare=True),
'steve':dict(hair=(.20,.13,.09),top=(.40,.39,.89),sleeve=(.40,.39,.89),pants=GRAY,style='jacket',haircut='cap',eyes='bright',long=True),
'jack':dict(hair=(.92,.91,.88),top=WHITE,sleeve=WHITE,pants=GRAY,style='coat',haircut='sweep',eyes='monocle',shirt=(.15,.14,.17),long=True),
'rod':dict(hair=(.22,.13,.09),top=(.30,.63,.92),sleeve=WHITE,pants=GRAY,style='vest',haircut='part',eyes='bright'),
}
# Filled only after reviewing actual native captures; no pre-authored round sweep.
REVISIONS={
 ('ishihara',2):dict(hairline_drop=.055,quarter_drop=.045,chin_width=.23,jaw_width=.30,cheek_width=.35,eye_shape='rect',eye_width=7,eye_height=8,mouth_width=8,review='Native i1 and independent review: restore swept front-quarter hair, adult jaw and rectangular eyes; strengthen mouth.'),
 ('imakuni',2):dict(hood_round=True,face_height=.45,review='Native i1: hood points look cat-like and lower mask covers open mouth; blunt ear tops and remap face opening.'),
 ('mitch',2):dict(eye_shape='slant',eye_width=8,eye_height=10,crown_rise=.035,review='Native i1: overly round eyes and small uniform hair crown; angular athletic eyes and staggered crown height.'),
 ('gene',2):dict(front_locks=[1.96,1.84,1.94,2.01,2.02],crown_rise=.055,eye_shape='rect',eye_width=7,eye_height=8,mouth_width=8,review='Native i1: pink hair reads as cap and eyes too round; broad diagonal forelock, asymmetric scalp and firmer adult face.'),
 ('isaac',2):dict(front_locks=[1.93,1.86,1.97,2.00,2.02],crown_rise=.04,eye_width=7,eye_height=10,review='Native i1: coat/glasses readable but smooth blond cap silhouette; add rooted diagonal fringe and crown wave.'),
 ('nikki',2):dict(bob_contour=[(.35,1.42),(.39,1.57),(.40,1.74),(.38,1.96)],front_locks=[1.95,1.88,1.95,2.01,2.02],eye_width=9,review='Native i1: bob flares into triangular side wings; reshape vertical rounded bob silhouette and clear soft eyes.'),
 ('murray',2):dict(bob_contour=[(.37,1.32),(.40,1.47),(.40,1.72),(.38,1.96)],hairline_drop=.025,quarter_drop=.025,eye_shape='rect',eye_width=7,eye_height=8,review='Native i1: rear long hair forms wings and face too youthful; smoother long receding contour and smaller rectangular mature eyes.'),
 ('rick',2):dict(front_locks=[1.96,1.85,1.95,2.01,2.02],crown_rise=.04,eye_shape='rect',eye_width=7,eye_height=9,firm_mouth=True,review='Native i1: brown cap-like scalp and smiling expression; restore brown swept fringe and stern scientist mouth.'),
 ('ken',2):dict(front_locks=[2.03,1.85,1.94,1.94,1.85],eye_shape='slant',eye_width=7,eye_height=10,mouth_width=8,review='Native i1: hair lacks source center part and face too round; two broad joined parted locks and angular competitive eyes.'),
 ('courtney',2):dict(bob_contour=[(.38,1.13),(.42,1.38),(.42,1.72),(.39,1.96)],front_locks=[1.89,1.96,1.94,1.94,1.96],eye_width=9,review='Native i1: long pink hair pinches into side wings; make continuous broader vertical waves and shaped central fringe.'),
 ('steve',2):dict(cap_crown=True,front_locks=[1.88,1.98,1.93,1.93,1.98],eye_width=7,eye_height=10,review='Native i1: cap is a tiny blue pancake over brown crown; insert fixed rim ring so blue crown owns full cap height.'),
 ('jack',2):dict(chin_width=.23,jaw_width=.30,cheek_width=.36,crown_rise=.05,eye_shape='rect',eye_width=7,eye_height=9,review='Native i1: moustache/monocle distinct but face too youthful; adult jaw, rectangular eyes and thicker swept white crown.'),
 ('rod',2):dict(front_locks=[1.97,1.83,1.93,1.99,2.02],crown_rise=.045,eye_width=7,eye_height=10,review='Native i1: source forelock absent and brown hair reads as cap; restore broad sloping forehead lock and rear sweep.'),

}

# Third-round changes were chosen after native round 2 inspection.
REVISIONS.update({
 ('ishihara',3):dict(REVISIONS[('ishihara',2)],mouth_rgb=(82,45,29),review='Native i2 + independent review: adult hair/jaw/eyes restored; darken short mouth to remove blank lower face.'),
 ('imakuni',3):dict(REVISIONS[('imakuni',2)],hood_nubs=True,review='Native i2 + independent review: mouth visible but hood still cat-like; lower and broaden two nub tops, retain hood continuity.'),
 ('mitch',3):dict(REVISIONS[('mitch',2)],spike_spread=True,crown_rise=0,chin_width=.21,mouth_rgb=(82,45,29),review='Native i2: round adult jaw still pinched and rear spikes merge; broaden/stagger rear points with modest central height and clearer short mouth.'),
 ('gene',3):dict(REVISIONS[('gene',2)],front_locks=[1.94,1.82,1.94,2.01,2.02],front_lock_depth=.05,review='Native i2 + independent review: pink scalp still cap-like; a deeper broad diagonal owning-shell forelock with forward depth, not a ribbon.'),
 ('isaac',3):dict(REVISIONS[('isaac',2)],glasses_aligned=True,front_locks=[1.93,1.83,1.97,2.00,2.02],front_lock_depth=.04,review='Native i2: frames cross pupil positions and blond fringe too smooth; align frame centers to eyes and deepen connected forelock.'),
 ('nikki',3):dict(REVISIONS[('nikki',2)],bob_contour=[(.33,1.44),(.38,1.57),(.40,1.74),(.38,1.96)],mouth_rgb=(90,48,30),review='Native i2: wings gone, but bob bottom too square and smile faint; taper lower rounded hem and strengthen small smile.'),
 ('murray',3):dict(REVISIONS[('murray',2)],glasses_aligned=True,chin_width=.23,jaw_width=.30,mouth_rgb=(90,48,30),review='Native i2: long receding profile restored but lenses misaligned and chin youthful; align lenses and broaden adult lower jaw.'),
 ('rick',3):dict(REVISIONS[('rick',2)],glasses_aligned=True,front_locks=[1.95,1.83,1.95,2.01,2.02],front_lock_depth=.04,mouth_rgb=(82,45,29),review='Native i2: glasses poorly align with eyes and brown fringe weak; realign frames, deepen owning fringe and retain stern expression.'),
 ('ken',3):dict(REVISIONS[('ken',2)],chin_width=.21,jaw_width=.29,mouth_rgb=(82,45,29),review='Native i2: source center part now reads; lower jaw still too pinched and mouth faint; broaden mature jaw and strengthen firm mouth.'),
 ('courtney',3):dict(REVISIONS[('courtney',2)],long_waves=True,hair=(.90,.38,.60),front_locks=[1.84,1.96,1.94,1.94,1.96],review='Native i2: wings gone but back hair flat and merges with gown; broad connected lower waves, deeper central fringe and darker pink separation.'),
 ('steve',3):dict(REVISIONS[('steve',2)],cap_badge=True,mouth_rgb=(90,48,30),review='Native i2: full crown corrected but source white cap patch absent; paint a small patch from unused costume atlas edge, no extra primitive.'),
 ('jack',3):dict(REVISIONS[('jack',2)],front_locks=[1.95,1.88,1.97,2.00,2.02],crown_rise=.07,review='Native i2: adult jaw/monocle/moustache read, white crown still too smooth; deeper swept owning forelock and broad quiff variation.'),
 ('rod',3):dict(REVISIONS[('rod',2)],front_locks=[1.95,1.83,1.96,1.99,2.02],front_lock_depth=.05,mouth_rgb=(90,48,30),review='Native i2: vest clear but large source sweep weak; deepen broad diagonal owning forelock and strengthen composed smile.'),
})
