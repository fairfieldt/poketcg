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
}
