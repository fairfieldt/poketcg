"""Distinct sprite silhouettes; palette variants never create another design."""
# Body, hair, face and costume are deliberately independent reusable modules.
# Bespoke silhouette details remain named, explicit additions per character.
DESIGNS = {
 'ishihara':dict(body='adult',hair='swept',face='mature',costume='vest',detail='receding collector hair and purple waistcoat'),
 'imakuni':dict(body='adult',hair='hood',face='imakuni',costume='bodysuit',detail='black bear-eared hood and full dark suit'),
 'tech':dict(body='adult',hair='flat',face='glasses',costume='labcoat',detail='square black hair, round spectacles, blue tie'),
 'clerk':dict(body='female',hair='bob',face='gentle',costume='uniform',hair_color='accent',detail='blue round bob, white collar, counter uniform'),
 'mania':dict(body='youth',hair='quiff',face='smile',costume='shirt',detail='large swept angular forelock, short-sleeved shirt'),
 'lass3':dict(body='girl',hair='twintails',face='gentle',costume='skirt',detail='two high black bunches with green ties'),
 'mitch':dict(body='athletic',hair='spikes',face='stern',costume='gi',detail='upright martial-artist spikes, open blue gi and black belt'),
 'boy':dict(body='youth',hair='sidepart',face='smile',costume='shirt',detail='side-parted short hair, light shirt; yellow material variant'),
 'man':dict(body='stout',hair='receding',face='mature',costume='shirt',detail='round cheeks, short receding hair and yellow polo'),
 'gene':dict(body='athletic',hair='swept',face='stern',costume='jacket',hair_color='accent',detail='red swept mining-club hair, red jacket, dark undershirt'),
 'swimmer':dict(body='girl',hair='cap',face='smile',costume='swimsuit',hair_color='accent',detail='shared pool outfit and rig; Sara swim cap/goggles and cheerful face, Amanda swept bob and asymmetric smile'),
 'joshua':dict(body='athletic',hair='cap',face='stern',costume='sailor',hair_color='white',detail='domed white swimming cap, forehead goggles, cheerful closed eyes; volumetric sleeveless top and shorts'),
 'amy':dict(body='girl',hair='bob',face='gentle',costume='sleeveless',hair_color='accent',detail='swept blue bob/nape, expressive blue eyes, white sleeveless dress; shared standing and lying geometry'),
 'girl':dict(body='girl',hair='long',face='smile',costume='skirt',detail='long black hair beneath blue cap, blue top and short skirt',cap=True),
 'hood':dict(body='youth',hair='points',face='stern',costume='hoodie',detail='two-point black fringe, red hooded top and dark trousers'),
 'isaac':dict(body='adult',hair='sidepart',face='glasses',costume='labcoat',hair_color='accent',detail='blond side part, glasses, open pale science coat'),
 'lass2':dict(body='girl',hair='long',face='gentle',costume='skirt',detail='long side-swept black hair and wide red headband',headband=True),
 'lass1':dict(body='girl',hair='bob',face='smile',costume='skirt',hair_color='accent',detail='rounded purple bob with a long side fringe'),
 'nikki':dict(body='female',hair='long',face='gentle',costume='dress',detail='long dark swept hair, green sleeveless dress and waist band'),
 'lad':dict(body='youth',hair='cap',face='smile',costume='shirt',hair_color='black',detail='green cap with short brim, green shirt and shorts',shorts=True),
 'murray':dict(body='stout',hair='bowl',face='round_glasses',costume='vest',detail='large round glasses, domed dark hair, pink shirt and dark vest'),
 'rick':dict(body='adult',hair='swept',face='glasses',costume='labcoat',detail='long side-swept dark science-club hair and round spectacles'),
 'specs':dict(body='adult',hair='sidepart',face='glasses',costume='shirt',detail='angular spectacles, tidy side part and purple shirt'),
 'ken':dict(body='athletic',hair='spikes',face='stern',costume='jacket',detail='low spreading spiky hair, red jacket and dark collar'),
 'courtney':dict(body='female',hair='long',face='gentle',costume='dress',hair_color='accent',detail='pink long hair with lifted side curls, high-neck Grand Master dress',curls=True),
 'steve':dict(body='adult',hair='cap',face='stern',costume='jacket',detail='indigo cap with long brim and white front badge, collared jacket'),
 'jack':dict(body='adult',hair='flat',face='stern',costume='suit',hair_color='white',detail='high pale square flat-top, white suit jacket and dark shirt'),
 'rod':dict(body='athletic',hair='curtains',face='stern',costume='vest',detail='center-parted black curtains, blue open vest and light shirt'),
 'woman':dict(body='female',hair='bob',face='gentle',costume='dress',hair_color='accent',detail='broad red bob with side part and red dress'),
 'chap':dict(body='adult',hair='quiff',face='mature',costume='vest',detail='high combed dark hair and green waistcoat'),
 'gal':dict(body='female',hair='short',face='gentle',costume='skirt',detail='short tousled dark hair, yellow top and dark skirt'),
 'pappy':dict(body='elder',hair='bald',face='old',costume='vest',detail='bald crown, side tufts, old brows and purple sweater vest'),
 'butch':dict(body='stout',hair='short',face='mature',costume='shirt',detail='wide head and thick short dark hair, yellow shirt'),
 'granny':dict(body='elder_female',hair='bun',face='old_glasses',costume='cardigan',detail='silver braided bun, small spectacles, yellow cardigan and long skirt'),
 'mark':dict(body='youth',hair='cap',face='mark',costume='trainer',detail='red cap with rear visor, rounded cheeks and original Mark facial paint; red vest, blue trousers and rounded backpack'),
 'torch':dict(body='prop',detail='faceted blue outer flame with pale inner flame and dark brazier'),
 'legendary_card':dict(body='prop',detail='four reusable rounded card quadrants and two faceted pink star sparks'),
}

# Overall scales keep the shared body parts proportional without forcing a
# child, athletic adult and elderly character into one silhouette.
BODIES={
 'adult':(1.0,1.0,1.0), 'youth':(.90,.92,.94),
 'athletic':(1.12,1.03,1.02), 'stout':(1.24,1.10,.94),
 'female':(.94,.92,1.0), 'girl':(.84,.85,.94),
 'elder':(1.07,1.02,.89), 'elder_female':(1.02,1.0,.88),
}
