#!/usr/bin/env python3
"""PIL-based visual analyzer for BioNeuronStudio GUI test screenshots."""
import os, sys, json
from PIL import Image

OUTDIR = "/tmp/studio_gui_tests"
REPORT = {"pass": 0, "fail": 0, "checks": []}

def check(name, cond, msg):
    REPORT["checks"].append({"name":name,"pass":cond,"msg":msg})
    if cond: REPORT["pass"]+=1
    else: REPORT["fail"]+=1; print(f"  [FAIL] {name}: {msg}")
    return cond

def load(path):
    if not os.path.exists(path): return None
    return Image.open(path)

def viewport_region(img):
    """Extract approximate 3D viewport region (center-left area)."""
    w,h=img.size
    return img.crop((w//10, h//10, w//2, h*9//10))

def pixel_stats(region):
    """Return dark/mid/bright percentages and color dominance."""
    px=list(region.getdata())
    n=len(px)
    dark=sum(1 for p in px if max(p)<40)
    mid=sum(1 for p in px if 40<=max(p)<150)
    bright=sum(1 for p in px if max(p)>=150)
    r=g=b=0
    for p in px: r+=p[0]; g+=p[1]; b+=p[2]
    return {"dark":dark*100//n,"mid":mid*100//n,"bright":bright*100//n,
            "avg":(r//n,g//n,b//n),"total":n}

def compare_screenshots(path1, path2, threshold=0.02):
    """Check if two screenshots differ significantly."""
    img1=load(path1); img2=load(path2)
    if not img1 or not img2: return False
    # Compare downsampled versions for speed
    s1=img1.resize((200,150)).getdata()
    s2=img2.resize((200,150)).getdata()
    diff=sum(1 for i,p in enumerate(s1) if sum(abs(a-b) for a,b in zip(p,s2[i]))>30)
    return diff/len(s1) > threshold

def main():
    if not os.path.isdir(OUTDIR):
        print("No screenshots directory"); sys.exit(1)

    files=sorted([f for f in os.listdir(OUTDIR) if f.endswith('.png')])
    if not files:
        print("No screenshots found"); sys.exit(1)

    print(f"=== PIL Analysis: {len(files)} screenshots ===")

    # 1. All screenshots are valid images
    for f in files:
        path=os.path.join(OUTDIR,f)
        img=load(path)
        check(f"valid_{f}", img is not None and img.size[0]>100, f"size={img.size if img else 'N/A'}")

    # 2. Viewport has content (not just blank panels)
    for f in files:
        path=os.path.join(OUTDIR,f)
        img=load(path)
        if img:
            vp=viewport_region(img)
            stats=pixel_stats(vp)
            has_content=stats["mid"]>5 or stats["bright"]>5 or stats["dark"]<95
            check(f"content_{f}", has_content,
                  f"dark={stats['dark']}% mid={stats['mid']}% bright={stats['bright']}% avg=rgb{stats['avg']}")

    # 3. Screenshots change between steps (not frozen)
    if len(files)>=2:
        for i in range(len(files)-1):
            p1=os.path.join(OUTDIR,files[i])
            p2=os.path.join(OUTDIR,files[i+1])
            changed=compare_screenshots(p1,p2)
            check(f"change_{files[i]}_to_{files[i+1]}", changed, "visual change detected" if changed else "NO change")

    # 4. Color diversity check (heatmap would show blue/red extremes)
    for f in files:
        path=os.path.join(OUTDIR,f)
        img=load(path)
        if img:
            vp=viewport_region(img)
            stats=pixel_stats(vp)
            # At least some color variation
            color_ok=stats["mid"]>3 or stats["bright"]>3
            check(f"colors_{f}", color_ok, f"mid={stats['mid']}% bright={stats['bright']}%")

    # Report
    print(f"\n=== VISUAL REPORT: {REPORT['pass']} PASS, {REPORT['fail']} FAIL ===")
    for c in REPORT["checks"]:
        status="PASS" if c["pass"] else "FAIL"
        print(f"  [{status}] {c['name']}: {c['msg']}")

    sys.exit(0 if REPORT["fail"]==0 else 1)

if __name__=="__main__":
    main()
