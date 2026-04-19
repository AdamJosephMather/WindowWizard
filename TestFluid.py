import matplotlib.pyplot as plt
import matplotlib.patches as patches
import random

HEIGHT, WIDTH = 1504, 2256

unused_colors = ['red', 'green', 'blue', 'cyan', 'yellow', 'black', 'orange', 'royalblue', 'gold', 'crimson']

class Box:
	x = 0
	y = 0
	w = 0
	h = 0
	c = 'r'

def showConfiguration(boxes, title="Final"):
	fig, ax = plt.subplots()
	plt.title(title)
	
	for box in boxes:
		rect = patches.Rectangle((box.x, box.y), box.w, box.h, linewidth=2, edgecolor='none', facecolor=box.c, alpha=0.5)
		ax.add_patch(rect)
	
	ax.set_xlim(0, WIDTH)
	ax.set_ylim(0, HEIGHT)
	ax.set_aspect('equal')
	ax.invert_yaxis()
	plt.show()

def getSum(a):
	return sum([i[0] for i in a])

def fixAreas(areas):
	if len(areas) == 0:
		return areas
	
	desired = WIDTH*HEIGHT
	current = getSum(AREAS)
	areas = [[int((i[0]/current)*desired), i[1]] for i in areas]
	missing = desired-getSum(areas)
	areas[-1][0] += missing
	return areas

def worst(row, w):
	"""Worst aspect ratio for a strip of width w containing the given areas."""
	if not row:
		return float('inf')
	s = sum(row)
	return max(w * w * max(row) / s**2, s**2 / (w * w * min(row)))

def layout_row(row, x, y, dx, dy):
	"""Place a committed row of items as a strip, return list of Box."""
	boxes = []
	if not row:
		return boxes
	s = sum(item[0] for item in row)
	if dx >= dy:                        # wider → vertical strip on the left
		col_w = s / dy
		cy = y
		for area, color in row:
			b = Box(); b.x = x; b.y = cy; b.w = col_w; b.h = area / col_w; b.c = color
			boxes.append(b); cy += b.h
	else:                               # taller → horizontal strip on the top
		row_h = s / dx
		cx = x
		for area, color in row:
			b = Box(); b.x = cx; b.y = y; b.w = area / row_h; b.h = row_h; b.c = color
			boxes.append(b); cx += b.w
	return boxes

def remaining_rect(row, x, y, dx, dy):
	"""Return (x, y, dx, dy) of the space left after committing the strip."""
	s = sum(item[0] for item in row)
	if dx >= dy:
		col_w = s / dy
		return x + col_w, y, dx - col_w, dy
	else:
		row_h = s / dx
		return x, y + row_h, dx, dy - row_h

def squarify(items, row, x, y, dx, dy):
	"""
	items : list of [area, color], sorted descending by area
	row   : current strip being built (list of [area, color])
	"""
	if not items:
		return layout_row(row, x, y, dx, dy)

	w = min(dx, dy)
	row_vals = [r[0] for r in row]

	if not row or worst(row_vals, w) >= worst(row_vals + [items[0][0]], w):
		# adding next item still improves (or ties) — keep growing the row
		return squarify(items[1:], row + [items[0]], x, y, dx, dy)
	else:
		# next item would worsen the row — commit current row and recurse
		boxes = layout_row(row, x, y, dx, dy)
		nx, ny, ndx, ndy = remaining_rect(row, x, y, dx, dy)
		return boxes + squarify(items, [], nx, ny, ndx, ndy)

def addborder(boxes):
	BORDER = 12
	for b in boxes:
		if b.x == 0:
			b.x = int(b.x+BORDER)
			b.w -= BORDER
		else:
			b.x = int(b.x+BORDER/2)
			b.w -= BORDER/2
		if b.y == 0:
			b.y = int(b.y+BORDER)
			b.h -= BORDER
		else:
			b.y = int(b.y+BORDER/2)
			b.h -= BORDER/2
		
		if b.x+b.w == WIDTH:
			b.w = int(b.w-BORDER)
		else:
			b.w = int(b.w-BORDER/2)
		if b.y+b.h == HEIGHT:
			b.h = int(b.h-BORDER)
		else:
			b.h = int(b.h-BORDER/2)
		
	return boxes

def recalculate(areas):
	if not areas:
		return []
	sorted_areas = sorted(areas, key=lambda x: x[0], reverse=True)
	boxes_raw = squarify(sorted_areas, [], 0, 0, WIDTH, HEIGHT)
	boxes = addborder(boxes_raw)
	return boxes

AREAS = []

print("""Actions:
	new
	delete (del)
	bigger (big)
	smaller (small)""")

while True:
	act = input(">").lower()
	
	if act == "new":
		if len(unused_colors) == 0:
			print("Not enough color!")
			continue
		
		if len(AREAS) == 0:
			AREAS.append([1, unused_colors.pop(0)])
		else:
			AREAS.append([getSum(AREAS)/len(AREAS), unused_colors.pop(0)]) # add the average area as a new block
		AREAS = fixAreas(AREAS)
	elif act == "del":
		delete = input("color to delete? ").strip().lower()
		for i in range(len(AREAS)):
			if AREAS[i][1] == delete:
				unused_colors.append(delete)
				AREAS.remove(AREAS[i])
				break
		AREAS = fixAreas(AREAS)
	elif act == "big":
		col = input("color to increase? ").strip().lower()
		for i in range(len(AREAS)):
			if AREAS[i][1] == col:
				AREAS[i][0] += (WIDTH*HEIGHT)*0.10
		AREAS = fixAreas(AREAS)
	elif act == "small":
		col = input("color to decrease? ").strip().lower()
		for i in range(len(AREAS)):
			if AREAS[i][1] == col:
				AREAS[i][0] -= (WIDTH*HEIGHT)*0.10
		AREAS = fixAreas(AREAS)
	
	BOXES = recalculate(AREAS)
	showConfiguration(BOXES)