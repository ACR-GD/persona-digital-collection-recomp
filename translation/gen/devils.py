# Generator for translation/en/devils.json: Kurouri's spell card collection (hidden; unlocked by hockey)
# Each caption strip: "<clan> <race>【Alignment】   <demon>". Demon names use established SMT English spellings.
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
S = {"_movie": "devils.dxr: 168 demon cards with captions; 'Crawly's spell card collection', VOICE, QUIT, BACK are English"}
S.update(yesno('devils', 9, 10, 11, 'Demon Boy: "I collect cards too, heh heh... wanna see? I\'ve got some amazing ones... fu fu fu."',
               ja='…:…? …'))
RACE = {'夜魔': 'Night', '妖魔': 'Fairy', '妖精': 'Fairy', '天使': 'Divine', '堕天使': 'Fallen', '龍王': 'Dragon King', '邪龍': 'Drake',
        '妖鳥': 'Avian', '凶鳥': 'Raptor', '魔獣': 'Beast', '妖獣': 'Wilder', '鬼女': 'Femme', '妖鬼': 'Yoma', '地霊': 'Jirae',
        '邪鬼': 'Brute', '幽鬼': 'Haunt', '悪霊': 'Spirit', '屍鬼': 'Undead', '外道': 'Foul', '業魔': 'Karma', '超人': 'Human',
        '魔人': 'Fiend', '魔王': 'Tyrant', '地母神': 'Mother', '死神': 'Reaper', '???': '???'}
CLAN = {'魔族': 'Demon', '飛天族': 'Sky', '龍族': 'Dragon', '鳥族': 'Bird', '獣族': 'Beast', '鬼族': 'Oni', '邪霊': 'Evil Spirit',
        '外道': 'Foul', '人間': 'Human', 'ボス': 'Boss'}
D = [  # (member, clan, race, name)
 (79,'魔族','夜魔','Jahi'),(81,'魔族','夜魔','Tlazolteotl'),(83,'魔族','夜魔','Succubus'),(85,'魔族','夜魔','Incubus'),
 (87,'魔族','夜魔','Hinoenma'),(89,'魔族','夜魔','Paligar'),(91,'魔族','夜魔','Lilim'),(93,'魔族','夜魔','Nightmare'),
 (95,'魔族','夜魔','Nacht Kobold'),(97,'魔族','妖魔','Ganesha'),(99,'魔族','妖魔','Xiuhtecuhtli'),(101,'魔族','妖魔','Tengu'),
 (103,'魔族','妖魔','Cupid'),(105,'魔族','妖魔','Jinn'),(107,'魔族','妖魔','Haokah'),(109,'魔族','妖魔','Gandharva'),
 (111,'魔族','妖魔','Agathion'),(113,'魔族','妖精','Oberon'),(115,'魔族','妖精','Titania'),(117,'魔族','妖精','Sumizome'),
 (119,'魔族','妖精','Poltergeist'[:0]+'Polisoon'),(121,'魔族','妖精','Dark Elf'),(123,'魔族','妖精','Rusalka'),(125,'魔族','妖精','Jack-o\'-Lantern'),
 (127,'魔族','妖精','Jack Frost'),(129,'魔族','妖精','Pixie'),(131,'飛天族','天使','Cherub'),(133,'飛天族','天使','Throne'),
 (135,'飛天族','天使','Dominion'),(137,'飛天族','天使','Virtue'),(139,'飛天族','天使','Power'),(141,'飛天族','天使','Principality'),
 (143,'飛天族','天使','Archangel'),(145,'飛天族','天使','Angel'),(147,'飛天族','堕天使','Adramelech'),(149,'飛天族','堕天使','Paimon'),
 (151,'飛天族','堕天使','Barbatos'),(153,'飛天族','堕天使','Berith'),(155,'飛天族','堕天使','Alastor'),(157,'飛天族','堕天使','Eligor'),
 (159,'飛天族','堕天使','Malphas'),(161,'飛天族','堕天使','Nisroc'),(163,'飛天族','堕天使','Ukobach'),(165,'龍族','龍王','Ouroboros'),
 (167,'龍族','龍王','Quetzalcoatl'),(169,'龍族','龍王','Otohime'),(171,'龍族','龍王','Mizuchi'),(173,'龍族','龍王','Naga'),
 (175,'龍族','龍王','Nozuchi'),(177,'龍族','龍王','Yato-no-Kami'),(179,'龍族','邪龍','Fafnir'),(181,'龍族','邪龍','Crom Cruach'),
 (183,'龍族','邪龍','Mushussu'),(185,'龍族','邪龍','Kiyohime'),(187,'龍族','邪龍','Humbaba'),(189,'龍族','邪龍','Wyvern'),
 (191,'龍族','邪龍','Cockatrice'),(193,'鳥族','妖鳥','Roc'),(195,'鳥族','妖鳥','Aello'),(197,'鳥族','妖鳥','Celaeno'),
 (199,'鳥族','妖鳥','Ocypete'),(201,'鳥族','妖鳥','Takuhi'),(203,'鳥族','妖鳥','Ba'),(205,'鳥族','凶鳥','Hraesvelgr'),
 (207,'鳥族','凶鳥','Alecto'),(209,'鳥族','凶鳥','Tisiphone'),(211,'鳥族','凶鳥','Megaera'),(213,'鳥族','凶鳥','Moh Shuvuu'),
 (215,'獣族','魔獣','Dun'),(217,'獣族','魔獣','Selket'),(219,'獣族','魔獣','Orthrus'),(221,'獣族','魔獣','Genkurou'),
 (223,'獣族','魔獣','Nekomata'),(225,'獣族','魔獣','Cu Sith'),(227,'獣族','魔獣','Cait Sith'),(229,'獣族','妖獣','Fenrir'),
 (231,'獣族','妖獣','Mandaguda'),(233,'獣族','妖獣','Catoblepas'),(235,'獣族','妖獣','Scylla'),(237,'獣族','妖獣','Black Widow'),
 (239,'獣族','妖獣','Cath Palug'),(241,'獣族','妖獣','Nue'),(243,'獣族','妖獣','Afanc'),(245,'鬼族','鬼女','Rangda'),
 (247,'鬼族','鬼女','Dakini'),(249,'鬼族','鬼女','Salome'),(251,'鬼族','鬼女','Alcina'),(253,'鬼族','鬼女','Iwate'),
 (255,'鬼族','鬼女','Siren'),(257,'鬼族','鬼女','Yomotsu-Shikome'),(259,'鬼族','妖鬼','Yaksa'),(261,'鬼族','妖鬼','Purski'),
 (263,'鬼族','妖鬼','Yakshini'),(265,'鬼族','妖鬼','Picollus'),(267,'鬼族','妖鬼','Dokkaebi'),(269,'鬼族','妖鬼','Hannya'),
 (271,'鬼族','妖鬼','Ihika'),(273,'鬼族','地霊','Uberri'),(275,'鬼族','地霊','Kawancha'),(277,'鬼族','地霊','Doppelganger'[:0]+'Dobergar'),
 (279,'鬼族','地霊','Kobold'),(281,'鬼族','地霊','Leprechaun'),(283,'鬼族','地霊','Knocker'),(285,'鬼族','邪鬼','Hecatoncheires'),
 (287,'鬼族','邪鬼','Girimehkala'),(289,'鬼族','邪鬼','Rakshasa'),(291,'鬼族','邪鬼','Ocelot'),(293,'鬼族','邪鬼','Sarashina-hime'),
 (295,'鬼族','邪鬼','Ogre'),(297,'鬼族','邪鬼','Gremlin'),(299,'邪霊','幽鬼','Miyasudokoro'),(301,'邪霊','幽鬼','Vetala'),
 (303,'邪霊','幽鬼','Druj'),(305,'邪霊','幽鬼','Yaka'),(307,'邪霊','幽鬼','Carey'),(309,'邪霊','幽鬼','Enku'),
 (311,'邪霊','幽鬼','Fuji-musume'),(313,'邪霊','幽鬼','Gaki'),(315,'邪霊','悪霊','Legion'),(317,'邪霊','悪霊','Kuchisake'),
 (319,'邪霊','悪霊','Hanako-san'),(321,'邪霊','悪霊','Bukimi-chan'),(323,'邪霊','悪霊','Dribbler'),(325,'邪霊','悪霊','Ghost'),
 (327,'邪霊','悪霊','Quicksilver'),(329,'邪霊','悪霊','Poltergeist'),(331,'邪霊','屍鬼','Zombie Painter'),(333,'邪霊','屍鬼','Zapp'),
 (335,'邪霊','屍鬼','Teke-Teke'),(337,'邪霊','屍鬼','Zombie Cop'),(339,'邪霊','屍鬼','Zombie Nurse'),(341,'邪霊','屍鬼','Zombie-chan'),
 (343,'邪霊','屍鬼','Zombie-kun'),(345,'邪霊','屍鬼','Zombie-san'),(347,'外道','外道','Gremies'),(349,'外道','外道','Shadow'),
 (351,'外道','外道','Doppelganger'),(353,'外道','外道','Anatomy'),(355,'外道','外道','Kokkuri-san'),(357,'外道','外道','Yakuza'),
 (359,'外道','外道','Punk'),(361,'外道','外道','Slime'),(363,'外道','業魔','Arrogance'),(365,'外道','業魔','Laziness'),
 (367,'外道','業魔','Bellicosity'),(369,'外道','業魔','Jealousy'),(371,'外道','業魔','Greed'),(373,'外道','業魔','Obscene'),
 (375,'人間','超人','Black Man'),(377,'人間','超人','Agent'),(379,'人間','超人','Secret Service'),(381,'人間','魔人','Alice'),
 (383,'ボス','超人','Takeda'),(385,'ボス','外道','Tesso'),(387,'ボス','邪鬼','Yog-Sothoth Jr.'),(389,'ボス','超人','Harem Queen'),
 (391,'ボス','業魔','Teddy Bear'),(393,'ボス','魔王','Saurva'),(395,'ボス','地母神','Hariti'),(397,'ボス','超人','Kandori'),
 (399,'ボス','夜魔','Hypnos'),(401,'ボス','鬼女','Nemesis'),(403,'ボス','死神','Thanatos'),(405,'ボス','業魔','Lady Masquerade'),
 (407,'ボス','???','God Kandori'),(409,'ボス','???','Pandora'),(411,'ボス','業魔','Lady Snow'),(413,'ボス','魔王','Queen Asura'),
 (415,'ボス','???','Big Eater-kun'),
]
ALIGN = {79: 'Evildark'}   # filled per caption below from the race (as printed on the cards)
ALIGN_OF = {'夜魔': 'Evildark', '妖魔': 'Elemental', '妖精': 'Elemental', '天使': 'Holylight', '堕天使': 'Evildark',
            '龍王': 'Holylight', '邪龍': 'Physical', '妖鳥': 'Elemental', '凶鳥': 'Physical', '魔獣': 'Holylight',
            '妖獣': 'Evildark', '鬼女': 'Physical', '妖鬼': 'Holylight', '地霊': 'Elemental', '邪鬼': 'Elemental',
            '幽鬼': 'Evildark', '悪霊': 'Evildark', '屍鬼': 'Physical', '外道': 'Physical', '業魔': 'Evildark',
            '超人': 'Normal', '魔人': 'Normal', '魔王': 'Evildark', '地母神': 'Physical', '死神': 'Evildark', '???': '?????'}
# exceptions printed on the cards
CARD_ALIGN = {387: 'Elemental', 413: '?????', 385: 'Physical'}
INK = [66, 49, 33]
for n, clan, race, name in D:
    im = load('devils', n); px = im.load(); w, h = im.size
    white_tail = im.getpixel((w - 1, h // 2)) == (255, 255, 255)   # 413: strip padded with white
    dark = [(x, y) for y in range(h) for x in range(w) if sum(px[x, y]) < 330]
    xs = [x for x, _ in dark]; ys = [y for _, y in dark]
    right = min(w, max(xs) + 2)
    t, b = max(0, min(ys) - 1), min(h, max(ys) + 2)
    al = CARD_ALIGN.get(n, ALIGN_OF[race])
    left = RACE[race] if clan != 'ボス' else ('Boss ' + RACE[race] if race != '???' else 'Boss')
    S[str(n)] = {"ja": "%s %s【%s】 %s" % (clan, race, al, name), "ops": [
        {"erase_dark": [0, 0, right if not white_tail else right, h], "thr": 520},
        {"text": "%s [%s]" % (left, al), "box": [4, t, 150, b], "valign": "middle", "font": "Times New Roman Bold.ttf", "size": 13, "color": INK, "nowrap": True},
        {"text": name, "box": [150, t, right - 2, b], "align": "right", "valign": "middle", "font": "Times New Roman Bold.ttf", "size": 16, "color": INK, "nowrap": True}]}
json.dump(S, open('translation/en/devils.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
