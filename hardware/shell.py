# Printable parts for the sphere: the black bowl that holds the board, and the ring it sits on.
# Board numbers come from Waveshare's 3D model of the ESP32-S3-LCD-1.85 (screen front at z=0, back is -z).
# Run: python hardware/shell.py  ->  hardware/bowl.stl, hardware/ring.stl
import math
from pathlib import Path
import cadquery as cq

OUT = Path(__file__).parent

BALL_D = 60.0      # outer diameter; must match the solid clear acrylic dome you buy (glued flat onto the rim)
WALL = 2.0         # bowl wall
CLEAR = 0.3        # fit clearance around the board

# Board outline: a 24mm circle on top; the bottom half bulges out (the "chin"), so it's given as
# its radius every 3 degrees from 180 to 357 (measured, padded to cover the gaps between samples).
BOARD_R = 24.0
CHIN_R = [24.1, 24.1, 24.2, 24.2, 24.2, 24.2, 24.2, 24.3, 24.3, 24.4, 24.4, 24.4, 24.5, 24.5, 24.6, 24.7, 24.8, 25.0,
          25.1, 25.3, 25.5, 25.7, 25.9, 26.1, 26.3, 26.9, 26.9, 26.9, 26.9, 26.7, 26.6, 26.7, 26.9, 27.2, 27.4, 27.4,
          27.4, 26.3, 26.1, 25.8, 25.6, 25.4, 25.2, 25.1, 25.0, 24.8, 24.7, 24.7, 24.6, 24.6, 24.5, 24.4, 24.4, 24.4,
          24.3, 24.3, 24.3, 24.2, 24.2, 24.1]
PCB_FRONT, PCB_BACK = -2.9, -4.5
DEPTH = 9.0                        # room behind the screen for the parts on the back
# Parts on the back that stick out past the outline: (xmin, xmax, ymin, ymax). Buttons, connectors, SD slot.
EDGE_PARTS = [(18.3, 22.8, 7.0, 12.1), (21.1, 24.8, -2.3, 2.3), (18.5, 23.1, -12.1, -7.0),
              (7.9, 15.3, -23.4, -16.2), (-15.0, -7.6, -23.2, -16.1), (-23.2, -15.9, -13.3, -2.8),
              (-22.1, -7.0, -0.1, 15.1), (6.0, 8.3, -24.9, -23.0), (-8.1, -5.9, -24.9, -23.0)]
STANDOFFS = [(15.7, -14.9), (15.7, 14.1), (-21.3, 0.0)]   # M2 posts under the board; it rests on these
STANDOFF_Z = -8.5                  # their bottom ends
MIC = (-16.15, -15.8)              # microphone; it listens through a hole in the board's back
USB_X, USB_Z = 0.2, -5.7           # USB-C port, facing -y at the board's edge
PLUG_W, PLUG_H = 13.5, 7.5         # window in the wall for the plug's head

R = BALL_D / 2
RI = R - WALL


def outline(grow, z0, z1):
    # The board's outline, grown by `grow`, as a solid from z0 to z1.
    radii = [BOARD_R] * 60 + CHIN_R
    pts = [(r * math.cos(math.radians(3 * i)), r * math.sin(math.radians(3 * i))) for i, r in enumerate(radii)]
    return cq.Workplane().polyline(pts).close().offset2D(grow).extrude(z1 - z0).translate((0, 0, z0))


def bowl():
    # Hemisphere shell, flattened slightly at the bottom so it prints without supports.
    shell = (cq.Workplane().sphere(R).cut(cq.Workplane().sphere(RI))
             .cut(cq.Workplane().box(BALL_D, BALL_D, R).translate((0, 0, R / 2)))
             .cut(cq.Workplane().box(BALL_D, BALL_D, 2).translate((0, 0, -R - 0.2))))

    # Solid collar around the board, so the gap around the screen is black plastic.
    # Its underside is a 45-degree cone so it prints without supports.
    r0 = BOARD_R + CLEAR
    collar = (cq.Workplane("XZ").polyline([(r0, 0), (R, 0), (R, -DEPTH - (R - r0)), (r0, -DEPTH)]).close()
              .revolve(360, (0, 0, 0), (0, 1, 0)))
    collar = collar.intersect(cq.Workplane().sphere(R - 0.01))

    # Pocket the board drops into, plus room for the parts that poke out past its edge.
    pocket = outline(CLEAR, -DEPTH - 0.5, 3)
    for x0, x1, y0, y1 in EDGE_PARTS:
        h = PCB_FRONT + DEPTH + 0.5
        pocket = pocket.union(cq.Workplane().box(x1 - x0 + 1, y1 - y0 + 1, h)
                              .translate(((x0 + x1) / 2, (y0 + y1) / 2, PCB_FRONT - h / 2)))

    # Posts the board's standoffs rest on, each with a pin into the standoff's hole to keep it from sliding.
    posts = None
    for x, y in STANDOFFS:
        p = (cq.Workplane().circle(2.5).extrude(R).translate((x, y, STANDOFF_Z - R))
             .union(cq.Workplane().circle(0.8).extrude(2).translate((x, y, STANDOFF_Z))))
        posts = p if posts is None else posts.union(p)
    posts = posts.intersect(cq.Workplane().sphere(R - 0.5))

    # Window through the wall for the USB-C plug, below the screen so the face's border stays unbroken.
    usb = cq.Workplane().box(PLUG_W, 12, PLUG_H).translate((USB_X, -R, USB_Z))

    # Sound hole: straight out from the microphone through the side of the ball.
    mx, my = MIC
    a = math.atan2(my, mx)
    mic = (cq.Workplane("YZ").circle(0.75).extrude(12)
           .rotate((0, 0, 0), (0, 0, 1), math.degrees(a)).translate((mx, my, PCB_BACK - 1.2)))

    return shell.union(collar).cut(pocket).union(posts).cut(usb).cut(mic)


RING_R, RING_W, RING_H = 13.0, 3.0, 4.0


def ring():
    # A short tube; its top edge is cut to the ball's curve so the ball sits snugly and can roll to any angle.
    tube = cq.Workplane().circle(RING_R + RING_W / 2).circle(RING_R - RING_W / 2).extrude(RING_H)
    ball_z = RING_H + math.sqrt(R * R - RING_R * RING_R) - 0.8
    return tube.cut(cq.Workplane().sphere(R).translate((0, 0, ball_z)))


if __name__ == "__main__":
    for name, part in (("bowl", bowl()), ("ring", ring())):
        cq.exporters.export(part, str(OUT / f"{name}.stl"), tolerance=0.02, angularTolerance=0.1)
        cq.exporters.export(part, str(OUT / f"{name}.step"))
        print("wrote", OUT / f"{name}.stl")
