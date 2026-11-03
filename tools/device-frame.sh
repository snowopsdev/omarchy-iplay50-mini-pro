#!/usr/bin/env bash
# Put a tablet screenshot (grim output, 1200x1920 or 1920x1200) into a drawn
# device frame with a soft shadow on a transparent background.
#
#   tools/device-frame.sh in.png out.png [width]
#
# width: final image width in pixels (default 640 portrait / 1000 landscape).
# The frame is a generic dark bezel drawn to the panel's proportions
# (108 x 172 mm active area, ~7 mm side and ~10 mm end bezels); it is not a
# picture of the real device. Needs ImageMagick 7 (`magick`).
set -euo pipefail

in=$1 out=$2
read -r w h < <(magick identify -format '%w %h\n' "$in")

if (( w > h )); then landscape=1; sw=$h sh=$w; else landscape=0; sw=$w sh=$h; fi
width=${3:-$(( landscape ? 1000 : 640 ))}

# Geometry in portrait, in screen pixels (~11.1 px per mm).
side=78 endb=112 radius=120 sradius=18 shadow=60
bw=$(( sw + 2 * side )) bh=$(( sh + 2 * endb ))
cw=$(( bw + 2 * shadow )) ch=$(( bh + 2 * shadow ))

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Screenshot rotated to portrait, with rounded screen corners.
if (( landscape )); then magick "$in" -rotate 90 "$tmp/screen.png"; else cp "$in" "$tmp/screen.png"; fi
magick -size "${sw}x${sh}" xc:none -fill white \
  -draw "roundrectangle 0,0 $((sw-1)),$((sh-1)) $sradius,$sradius" "$tmp/smask.png"
magick "$tmp/screen.png" -alpha set "$tmp/smask.png" -compose DstIn -composite "$tmp/screen-r.png"

x0=$shadow y0=$shadow x1=$(( shadow + bw - 1 )) y1=$(( shadow + bh - 1 ))
cam_x=$(( cw / 2 )) cam_y=$(( shadow + endb / 2 ))

magick -size "${cw}x${ch}" xc:none \
  \( -size "${cw}x${ch}" xc:none -fill 'rgba(0,0,0,0.55)' \
     -draw "roundrectangle $x0,$((y0+18)) $x1,$((y1+18)) $radius,$radius" -blur 0x28 \) -composite \
  -fill '#2c2d31' -draw "roundrectangle $x0,$y0 $x1,$y1 $radius,$radius" \
  -fill '#111214' -draw "roundrectangle $((x0+5)),$((y0+5)) $((x1-5)),$((y1-5)) $((radius-5)),$((radius-5))" \
  -fill none -stroke '#45474d' -strokewidth 2 \
  -draw "roundrectangle $((x0+1)),$((y0+1)) $((x1-1)),$((y1-1)) $radius,$radius" \
  -stroke none -fill '#1d1f24' -draw "circle $cam_x,$cam_y $((cam_x+9)),$cam_y" \
  -fill '#2b3550' -draw "circle $cam_x,$cam_y $((cam_x+4)),$cam_y" \
  "$tmp/screen-r.png" -geometry "+$(( shadow + side ))+$(( shadow + endb ))" -composite \
  "$tmp/framed.png"

if (( landscape )); then magick "$tmp/framed.png" -rotate -90 "$tmp/framed.png"; fi
magick "$tmp/framed.png" -filter Lanczos -resize "${width}x" -depth 8 -strip \
  -define png:compression-level=9 "$out"
echo "$out"
