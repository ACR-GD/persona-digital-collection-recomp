# Generator for translation/en/leader.json: Philemon's leadership aptitude test
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
from textpanel import find_interior
S = {"_movie": "leader.dxr: Philemon's leadership test (title card 'Phylemon's leadership ability test' and YES/NO are already English)"}
S.update(yesno('leader', 13, 14, 15, 'Philemon: "Let me assess how suited you are to be a leader, and whether you truly deserve to be one."',
               ja='…:…'))
NAVY = [0, 33, 82]
qs = {23: "I know every friend's special skill.", 24: "I tend to end up walking in front.",
      25: "Before I know it, I'm often the one running things.", 26: "When I buy snacks, I always buy some for my friends too.",
      27: "When I'm with friends, I'm quick to ask \"Aren't you hungry?\"", 28: "I'll even take on fights my friends got into.",
      29: "Many of my friends are oddballs.", 30: "I'm a bad singer.",
      31: "I'm quiet. I don't talk much unless I have to.", 32: "When I talk with people, I find myself lecturing them."}
for n, en in qs.items():
    im = load('leader', n); px = im.load(); w, h = im.size
    xs = sorted(x for x in range(w) if any(sum(px[x, y]) < 200 for y in range(h)))
    end = xs[0]
    for x in xs[1:]:
        if x - end <= 3: end = x
        else: break
    box = [end + 6, 0, w, h]
    S[str(n)] = {"ja": "(question %d)" % n, "ops": [{"erase": box, "thr": 0, "sat": 255, "halo": 0} if False else {"fill": box, "color": [255, 255, 255]},
        {"restore_bg": True}]}
    # the background is a light marbled texture: rebuild it from the clean rows by erasing dark text
    S[str(n)] = {"ja": "(question %d)" % n, "ops": [{"erase_dark": box},
        {"text": en, "box": [box[0] + 4, 1, w - 4, h - 1], "align": "center", "valign": "middle", "font": "Georgia Bold.ttf", "size": 12, "color": NAVY}]}
res = {
 34: (100, "With both self-control and ambition, you are a born leader. Leading a group of strong personalities should not be that hard for you."),
 36: (90, "Decisive, sensible and conscientious, you are the dependable big-sister type. Aside from a critical, idealistic streak, you are a flawless leader."),
 38: (85, "You see yourself as self-confident. With courage, drive and self-control, you could be a constructive leader, but your grandiose side is a flaw."),
 40: (82, "Devoted, responsible and trustworthy, you lead naturally among close friends. See that your rebelliousness is really dependence, and you will lead many more."),
 42: (64, "Easygoing and tolerant, you never lose your cool in a crisis, and those around you see you as reliable. But aren't you straining to live up to that image?"),
 44: (58, "Self-assertive and highly adaptable, you are a popular smooth talker. Overcome your lack of backbone and your narcissism, and you'll show the makings of a leader."),
 46: (46, "Everyone has two sides, but your ideals and reality are far apart. Face reality calmly to get closer to your ideal. The people you envy struggle too."),
 48: (3, "Self-destructive, dependent, paranoid and combative: leading is hard for you. Try getting along with all kinds of people; a magic trick could break the ice."),
 50: (33, "You are an impulsive, greedy pragmatist. In other words, a realistic, outgoing doer. You fear loneliness more than you need to; you'll need the strength to overcome it."),
}
for n, (pct, en) in res.items():
    im = load('leader', n); l, t, r, b = find_interior(im.crop((130, 0) + im.size))
    # the Japanese text overflows the inner frame on the right: erase up to the outer bevel
    S[str(n)] = {"ja": "…%d%% ..." % pct, "box": [l + 131, t, r + 131, b + 2], "text_box": [l + 133, t + 2, r + 129, b + 2],
                 "en": "Leadership aptitude: %d%%\n%s" % (pct, en), "size": 12, "leading": 1.18}
json.dump(S, open('translation/en/leader.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
