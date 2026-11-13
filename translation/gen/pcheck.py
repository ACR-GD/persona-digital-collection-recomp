# Generator for translation/en/pcheck.json: Igor's Persona check (traveling Velvet Room)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
from textpanel import find_interior
S = {"_movie": "pcheck.dxr: Igor's Persona check: 15 yes/no questions, then a Persona and its spell card number"}
S.update(yesno('pcheck', 11, 12, 13, 'Igor: "I shall find the Persona that suits you best."',
               ja='…:…'))
qs = {19: "The tougher things get, the more motivated I become.", 21: "I think I have a split personality.",
      23: "I can't rest unless the garbage is properly sorted.", 25: "Somehow I always end up as group leader or party organizer.",
      27: "There are so many things I want to do.", 29: "When I lie, it shows on my face.",
      31: "I don't find other people's opinions very useful.", 33: "People often call me \"big sis\" or \"boss\".",
      35: "I'm the type who holds a grudge.", 37: "Lately I've been really popular.",
      39: "I never forget anything in a book I've read once.", 41: "I love new things but get bored quickly.",
      43: "I love plotting pranks and telling tall tales.", 45: "At camps and amusement parks, I play my heart out.",
      47: "I often daydream and lose track of time."}
for n, en in qs.items():
    S[str(n)] = {"ja": "(question %d)" % n, "box": [0, 0] + list(load('pcheck', n).size), "en": en, "align": "center",
                 "valign": "middle", "size": 15, "nowrap": False}
def portrait_panel(n, en, ja):
    im = load('pcheck', n); l, t, r, b = find_interior(im.crop((130, 0) + im.size))
    S[str(n)] = {"ja": ja, "box": [l + 132, t + 2, r + 131, b], "en": en}
portrait_panel(49, 'Igor: "I have found the Persona for you. Please enter its spell card number into the Evilphone."',
               '…:…')
res = {
 121: ("WORLD Mucalinda", "you, whose mind is heightened and focused right now"),
 123: ("TOWER Amatsu-Mikaboshi", "you, who can be a little contrary"),
 125: ("MOON Succubus", "you, a witty and playful soul"),
 127: ("HIEROPHANT Aizen-Myoh", "you, pure, upright and noble, who hates anything crooked"),
 129: ("EMPRESS Vesta", "you, the strict but kind big-sister type everyone relies on"),
 131: ("SUN Suzaku", "you, innocent as a child and full of energy"),
 133: ("JUSTICE Neamhain", "you, honest to the core and unable to lie"),
 135: ("DEATH Hel", "you, who are forever seeking change"),
 137: ("EMPEROR Seimen Kongo", "you, the quiet pillar who looks after everyone"),
 139: ("CHARIOT Ogun", "you, stubborn and with a craftsman's spirit"),
 141: ("FOOL Narukami", "you, who hold limitless potential"),
 143: ("MAGICIAN Houri", "you, a doer who can never sit still"),
 145: ("LOVERS Pixie", "you, full of affection and brimming with charm"),
 147: ("PRIESTESS Mazu", "you, with your enigmatic, mysterious charm"),
 149: ("DEVIL Bres", "you, who hold to your principles in everything"),
 151: ("JUDGEMENT Nike", "you, the type who catches fire in a pinch"),
 153: ("HERMIT Byakko", "you, the walking encyclopedia of your circle of friends"),
 155: ("TEMPERANCE Genbu", "you, who value balance and harmony in all things"),
}
for n, (persona, who) in res.items():
    portrait_panel(n, 'Igor: "For %s, I recommend the Persona %s."' % (who, persona), '…:…"%s"…' % persona)
json.dump(S, open('translation/en/pcheck.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
