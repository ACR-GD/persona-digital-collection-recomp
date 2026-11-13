# Generator for translation/en/photo.json: Yukino's photo exhibit (hidden; unlocked by the race)
# 40 characters: a profile card (100-139) and a story panel (140-179) each.
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
S = {"_movie": "photo.dxr: Yukino's photo exhibit: character gallery with profile and story; PREV/NEXT/BACK and PICTURE/PROFILE/STORY labels are English"}
S.update(yesno('photo', 11, 12, 13, 'Yukino: "Wanna see the photos I took?"', ja='…:…?…'))
S["18"] = {"ja": "…:…? …", "box": [138, 308, 472, 393], "text_box": [141, 311, 470, 393],
           "en": 'Yukino: "Which photo d\'you wanna see? Hurry up and pick."'}
HS = "High school student\n   (St. Hermelin High)"
cards = {
 100: "Protagonist\nOccupation: " + HS,
 101: "Maki\nReal name: Maki Sonomura\nBirthday: 6/4   Blood type: AB\nHeight: 158cm   Shoes: 23.5cm\nEyesight: 1.5   Hobby: Painting\nFavorite color: Red\nFavorite food: Strawberry sweets\nFavorite book: \"The Door of Paradise\"\nFamily: Her mother\nDays off: Shopping with Mom",
 102: "Mark\nReal name: Masao Inaba\nBirthday: 7/11   Blood type: A\nHeight: 167cm   Weight: 52kg\nShoes: 25.5cm   Eyesight: 2.0\nFavorite color: Yellow\nHobbies: Graffiti and dance\nFavorite food: Chicken combo\nFavorite magazine: \"BOON\"\nFamily: Mother and father\nStyle: Hats",
 103: "Nanjo\nReal name: Kei Nanjo\nBirthday: 10/2   Blood type: A\nHeight: 177cm   Weight: 62kg\nShoes: 26.5cm   Eyesight: 0.08\nFavorite color: Blue\nFamily: Yamaoka\nHobbies: Fencing, hunting\nFavorite food: Japanese cuisine\nFavorite magazine: \"President\"\nStyle: Scarf knit by Yamaoka\nDays off: Classical music, reading",
 104: "Yukino\nReal name: Yukino Mayuzumi\nBirthday: 4/9   Blood type: A\nHeight: 170cm   Weight: 52kg\nShoes: 24.5cm   Eyesight: 1.5\nFavorite color: White\nHobby: Photography\nFavorite food: Yakisoba\nFavorite magazine: \"Asahi Camera\"\nFamily: Mother, 2 sisters, 2 brothers\nStyle: Long skirts\nFavorite fashion: agnes b.",
 105: "Ayase\nReal name: Yuka Ayase\nBirthday: 3/3   Blood type: B\nHeight: 155cm   Shoes: 24.5cm\nEyesight: 2.2\nHobby: Purikura   Fav. color: Black\nFavorite food: Engawa sushi\nFavorite magazine: \"anan\"\nStyle: Avant-garde\nFavorite perfume: POISON\nFavorite fashion: LOVE BOAT",
 106: "Brown\nReal name: Hidehiko Uesugi\nSign: Libra\nBlood type: O\nFavorite color: Brown\nFavorite reading: \"Comic BonBon\"\nStyle: Goggles and boots\nFavorite food: Hamburg steak and curry",
 107: "Elly\nReal name: Eriko Kirishima\nBirthday: 9/21   Blood type: O\nHeight: 164cm   Shoes: 24cm\nEyesight: 0.5\nHobby: Occult research\nFavorite color: Violet\nFavorite food: GODIVA chocolate\nFavorite magazines: \"ELLE\", \"Mu\"\nFamily: Parents, twin sisters\nFavorite perfume: ALLURE\nFavorite fashion: DKNY",
 108: "Reiji\nReal name: Reiji Kido\nBirthday: 8/18   Blood type: B\nHeight: 182cm   Weight: 75kg\nShoes: 27cm   Eyesight: 1.8\nFavorite color: Black\nHobby: Magic tricks\nFavorite food: His mom's cooking\nFavorite magazine: \"Kakutogi Tsushin\"\nFamily: His mother\nStyle: Chain choker\nDays off: Working on his abs",
 109: "The girl in white\nKnown as: Mai",
 110: "The girl in black\nKnown as: Aki",
 111: "Maki's mother\nReal name: Setsuko Sonomura\nOccupation: Researcher\n   (SEBEC Mikage branch)",
 112: "Ms. Saeko\nReal name: Saeko Takami\nOccupation: Teacher",
 113: "Ms. Natsumi\nReal name: Natsumi Yoshino\nOccupation: School nurse,\n   St. Hermelin High",
 114: "Vice Principal Hanya\nOccupation: Vice principal, St. Hermelin High",
 115: "Principal Oishi\nOccupation: Principal, St. Hermelin High",
 116: "Butler Yamaoka\nOccupation: Butler (Nanjo)",
 117: "Real name: Yosuke Naito\nOccupation: " + HS,
 118: "Real name: Chisato Kousai\nOccupation: " + HS,
 119: "Demon Boy\nReal name: Tsutomu Kurouri\nOccupation: " + HS,
 120: "Real name: Yuko Himeno\nOccupation: " + HS,
 121: "Toro\nReal name: Kenta Yokouchi\nOccupation: " + HS,
 122: "Tadashi\nReal name: Tadashi Satomi\nOccupation: " + HS,
 123: "Tamaki\nOccupation: " + HS,
 124: "Real name: Takahisa Kandori\nOccupation: President, SEBEC Mikage branch\nAge: 28\nBirthday: November 8\nBlood type: A\nHeight: 182cm\nWeight: 71kg\nEducation: Oxford University graduate school, UK\nFamily: Both parents deceased; single",
 125: "Takeda\nOccupation: Representative,\n   SEBEC Security Services",
 126: "Dr. Nicolai\nOccupation: Researcher\n   (SEBEC Mikage branch)",
 127: "Real name: Kazumi Fujimori\nOccupation: " + HS,
 128: "Master of the Tower of Hypnos\nReal name: Kumi Hirose\nOccupation: " + HS,
 129: "Master of the Tower of Nemesis\nReal name: Michiko Matsudaira\nOccupation: " + HS,
 130: "Master of the Tower of Thanatos\nReal name: Yuriko Yamamoto\nOccupation: High school student\n   (St. Hermelin High?)",
 131: "Tadashi's father\nOccupation: Runs a pharmacy",
 132: "Igor\nOccupation: Mindmancer",
 133: "Trish\nOccupation: Fairy",
 134: "Sennen Mannen-do owner\nOccupation: Candy shop owner",
 135: "Tsurun-Khamen\nOccupation: Antique dealer",
 136: "Khamen-Tsurun\nOccupation: Antique dealer",
 137: "Mannequin\nOccupation: Boutique clerk",
 138: "Clerk\nOccupation: Convenience store clerk",
 139: "Bartender\nOccupation: Bartender",
}
stories = {
 140: "In class 2-4 at St. Hermelin High. Said to be taciturn, yet trusted enough to be given the final say in big decisions. Unobtrusive, but the heart of the party.",
 141: "The protagonist's classmate, in the art club. Hospitalized at Mikage General Hospital since a year ago. Envies school life and fears for her future. She had a crush on Yosuke Naito.",
 142: "The protagonist's classmate. Member of the graffiti and dance team \"Tailors\". He sees talent in Maki's paintings and has feelings for her. Popular in his team, with a strong sense of responsibility.",
 143: "The protagonist's classmate. Heir to the Nanjo conglomerate. Having received his parents' love only as money, he cannot express warmth and opens his heart only to his butler, Yamaoka.",
 144: "The protagonist's classmate. With a life outside school, she finds her classmates childish. Meeting Ms. Saeko turned her life around. She works part-time to save money to study abroad.",
 145: "The protagonist's classmate. Anxious about her future, she drowns it in a hedonistic lifestyle. Her emotional, impulsive words sometimes set incidents in motion.",
 146: "The protagonist's classmate. His fashion sense and banter make him popular in and out of school, but he feels stifled by the false image he plays up.",
 147: "The protagonist's classmate. A returnee whose looks and manners draw attention in and out of school; to live up to those expectations, she suppresses her true self.",
 148: "The protagonist's classmate. Transferred to St. Hermelin six months ago. He hates the Kandori family and lives only for revenge on Kandori. He opens his heart to no one but his mother and refuses all contact.",
 149: "The girl in white who appears at the start of the story: a convenient helper who runs the world for the ideal Maki. At first the whole town was a paradise she alone managed, but Kandori's arrival gave rise to \"Aki\".",
 150: "The girl in black. She appeared when Kandori entered Maki's ideal world. Born of Maki's longing for her father, she does as Kandori says and torments the protagonists with the compact's magic.",
 151: "Maki's mother. She worked on the DEVA System. As it was a vital SEBEC secret, its researchers were all but locked away, so she could rarely visit her hospitalized daughter. The compact she gave her lonely girl became the key item of the incident.",
 152: "A St. Hermelin teacher in charge of the class of problem students. An alumna and former drama club member who starred in the legendary play \"The Snow Queen\". Her frankness wins every student's admiration; even Yukino respects her.",
 153: "The kind school nurse who always truly worries about the protagonists. Her mature charm and homely air make her popular with the boys of St. Hermelin. But in fact, she's a terrible cook.",
 154: "A vice principal whose personality matches his scary looks: the most hated teacher at St. Hermelin. He dislikes Principal Oishi's way of educating and does nothing but complain.",
 155: "A friendly principal who greets even students she just passes in the hall. She always puts the students first: a model educator.",
 156: "A butler by title, but to Nanjo, ignored by his parents, he was the only one who gave him love; the two share a deep bond. Naturally, the \"No. 1 scarf\" is his handiwork.",
 157: "Chisato Kousai's boyfriend, and the one Maki admired. Rumored to have vanished, he meets the protagonists in the world inside Maki's heart and helps them resolve the incident.",
 158: "Maki's best friend, yet jealous of her talent as a painter. She met Aki and, in exchange for the throne of the Harem Queen, became a hideous, ragged figure. Thanks to the protagonists and her boyfriend Yosuke, she regained her form and made up with Maki.",
 159: "As his nickname suggests, his hobby is occult research; he can always be found looking things up in the St. Hermelin library. Incidentally, Detective Isono of \"Shin Megami Tensei: Devil Summoner\" is his uncle.",
 160: "Maki's classmate. Maki even lent her \"The Door of Paradise\", her most treasured book, so they must be close. Yet in the Mikage-cho of Maki's heart, her change in attitude is a worry.",
 161: "He loves eating; his hobbies are raising and collecting turtles. His feelings for Ayase never reached her, and, cornered, his Persona awakened: a tragic one that turned his body into a demon.",
 162: "Eldest son of the Satomi family behind the \"Satomi Tadashi Pharmacy\". He's always fighting with Tamaki, though it goes without saying that it's because he likes her so much. Let's watch over them warmly.",
 163: "A bright, energetic girl, and a mysterious transfer student. She seems to have been caught up in a big incident at her old school. She's always fighting with Tadashi, but she may be truly happy that she can now.",
 164: "President of Saeki Electronics & Biological & Energy Corporation (SEBEC), Mikage branch. A former St. Hermelin student council president. Head of the DEVA System project, he is the man behind Mikage-cho's transformation.",
 165: "Head of SEBEC's private security force sounds nice, but his dirty work included things like delivering hush money to Kandori's mistress. Actually, he dreamed of being a singer.",
 166: "Inventor of the Dimension Variable Accelerator System, the \"DEVA System\", developed under Kandori. Learning his research was being misused, he sacrifices himself to take responsibility.",
 167: "The culprit who froze Ms. Saeko and set the protagonists' trials. Ms. Saeko's close friend since their own school days, she envied Saeko's personality and looks. Her pent-up feelings exploded in the \"Snow Queen incident\", giving birth to the Persona \"Snow Queen\".",
 168: "Withdrawn and pessimistic, she feared harsh reality and became a servant of the \"Snow Queen\", who gave her a world of dreams. Her flashbacks in the dream world make you understand, if not excuse, why she was bullied.",
 169: "A girl of an old noble family who never doubts her own beauty, and was shunned and disliked by classmates for her arrogance. Resentful, she became a demon of vengeance. She alone shows no remorse even after defeat.",
 170: "As her retro '80s idol look suggests, she is obsessed with being \"cute\". Fearing time would take it, she sought eternal beauty and surrendered to the \"Snow Queen\". Her tearful story she tells you herself after the battle.",
 171: "Tadashi's father and founder of the \"Satomi Tadashi Pharmacy\". All three current stores are family-run as a chain. Aiming to go national, he is planning regional dialect versions of the theme song. His rival: a certain drugstore chain.",
 172: "Mindmancer Igor grants \"Personas\", products of the spirit world. A follower of Philemon, his role is to create \"triggers\" that change people's perception. With the Evilphone he summons demons and turns them into \"beings of the human heart\": his art of fusion.",
 173: "Trish, the fairy guarding the \"Healing Spring\". She was apparently banished from the fairy world for outrageous pranks, and since she still charges a fortune to help people in Mikage-cho, she can never go home.",
 174: "The candy shop with the auspicious name \"Sennen Mannen-do\". Its owner in a casual kimono leaves a strong impression. Judging by the big beckoning-cat figurine in the shop, his love of lucky charms is no joke.",
 175: "The item shop selling handy goods in transformed Mikage-cho. It seems to be one of the Personas born of the candy shop owner's heartfelt wish: \"Everyone, live a thousand, ten thousand years!\"",
 176: "The only rug(?) shop in Mikage-cho, where gems are traded for handy items. It too seems to be a Persona born of the candy shop owner's heartfelt wish: \"Everyone, live a thousand, ten thousand years!\"",
 177: "For clothes in Mikage-cho, this is the place: the boutique \"ROSSA CANDITA\". Its mannequins give off a punk vibe, but it sells more than those clothes. Incidentally, Brown seems to like her.",
 178: "The \"YIN&YAN\" convenience store chain. In Maki's inner world it stocks goods you'd never imagine. The clerk who smiles, asks \"Looking for a weapon?\" and holds out a rifle never gets less unnerving.",
 179: "The Black Market, the town made by the Harem Queen and Aki. He runs its only bar. In a town full of flashy afros and funky characters, he is the one calm grown-up.",
}
from textpanel import lum
def right_edge(im):
    # some panels carry a raised frame on the right: stop before columns that are mostly frame
    px = im.load(); w, h = im.size
    def frame(x): return sum(1 for y in range(h) if (px[x, y][2] > 150 and px[x, y][0] < 160) or lum(px[x, y]) > 180) > 0.6 * h
    r = w
    while r > 3 * w // 4 and frame(r - 1): r -= 1
    return min(w - 2, r - 1)
for n, en in cards.items():
    im = load('photo', n); w, h = im.size; r = right_edge(im)
    S[str(n)] = {"ja": "(profile card %d)" % n, "box": [2, 2, r, h - 2], "text_box": [4, 2, r - 1, h - 1], "en": en,
                 "size": 12, "leading": 1.12, "font": "Georgia Bold.ttf", "halo": 3}
    if w < 170: S[str(n)]["nowrap"] = True      # narrow cards: shrink instead of wrapping names
for n, en in stories.items():
    im = load('photo', n); w, h = im.size; r = right_edge(im)
    S[str(n)] = {"ja": "(story %d)" % n, "box": [2, 2, r, h - 2], "text_box": [5, 3, r - 2, h - 1], "en": en,
                 "size": 12, "leading": 1.15, "halo": 3}
# Elly's birthday "9/21" runs onto the frame bevel: erase that part by copying the frame from above/below
e = S["107"]
S["107"] = {"ja": e["ja"], "ops": [{"erase": e["box"], "halo": 3}, {"erase": [e["box"][2], 12, 231, 34], "vertical": True},
            {"text": e["en"], "box": e["text_box"], "font": e["font"], "size": e["size"], "leading": e["leading"]}]}
json.dump(S, open('translation/en/photo.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
