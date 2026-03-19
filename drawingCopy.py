import pygame
import math

class DrawingCanvas:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.canvas = pygame.Surface((width, height))
        self.background_color = (255, 255, 255)
        self.current_color = (0, 0, 0)
        self.pen_size = 5
        self.min_size = 1
        self.max_size = 50
        self.eraser_mode = False
        self.last_pos = None
        self.points = []
        self.undo_stack = []
        self.redo_stack = []
        self.max_undo = 100
        self.clear_canvas()

    def clear_canvas(self):
        self.canvas.fill(self.background_color)
        self.save_state()

    def set_color(self, color):
        self.current_color = color
        self.eraser_mode = False

    def set_size(self, size):
        self.pen_size = max(self.min_size, min(self.max_size, size))

    def toggle_eraser(self):
        self.eraser_mode = not self.eraser_mode

    def get_drawing_color(self):
        return self.background_color if self.eraser_mode else self.current_color

    def save_state(self):
        state = pygame.image.tostring(self.canvas, "RGB")
        self.undo_stack.append(state)
        if len(self.undo_stack) > self.max_undo:
            self.undo_stack.pop(0)
        self.redo_stack.clear()

    def undo(self):
        if len(self.undo_stack) > 1:
            self.redo_stack.append(self.undo_stack.pop())
            prev_state = self.undo_stack[-1]
            self.canvas = pygame.image.fromstring(prev_state, (self.width, self.height), "RGB")

    def redo(self):
        if self.redo_stack:
            state = self.redo_stack.pop()
            self.undo_stack.append(state)
            self.canvas = pygame.image.fromstring(state, (self.width, self.height), "RGB")

    def start_stroke(self, pos):
        self.last_pos = pos
        self.points = [pos]
        self.save_state()
        self._draw_point(pos)

    def continue_stroke(self, pos):
        if self.last_pos is None:
            return
        self._draw_line(self.last_pos, pos)
        self.last_pos = pos
        self.points.append(pos)

    def end_stroke(self):
        self.last_pos = None
        self.points = []

    def _draw_point(self, pos):
        color = self.get_drawing_color()
        pygame.draw.circle(self.canvas, color, (int(pos[0]), int(pos[1])), self.pen_size)

    def _draw_line(self, start, end):
        color = self.get_drawing_color()
        dx = end[0] - start[0]
        dy = end[1] - start[1]
        distance = math.hypot(dx, dy)
        if distance < 1:
            self._draw_point(end)
            return
        steps = max(int(distance / 2), 1)
        for i in range(steps + 1):
            t = i / steps
            x = int(start[0] + dx * t)
            y = int(start[1] + dy * t)
            pygame.draw.circle(self.canvas, color, (x, y), self.pen_size)

    def toggle_background(self):
        old_bg = self.background_color
        new_bg = (0, 0, 0) if old_bg == (255, 255, 255) else (255, 255, 255)
        new_canvas = pygame.Surface((self.width, self.height))
        new_canvas.fill(new_bg)

        def near_black_or_white(c, tol=50):
            return (c[0] < tol and c[1] < tol and c[2] < tol) or \
                   (c[0] > 255-tol and c[1] > 255-tol and c[2] > 255-tol)

        for x in range(self.width):
            for y in range(self.height):
                pixel = self.canvas.get_at((x, y))
                if (abs(pixel[0] - old_bg[0]) < 30 and
                    abs(pixel[1] - old_bg[1]) < 30 and
                    abs(pixel[2] - old_bg[2]) < 30):
                    continue
                if near_black_or_white(pixel):
                    inv = (255 - pixel[0], 255 - pixel[1], 255 - pixel[2])
                    new_canvas.set_at((x, y), inv)
                else:
                    new_canvas.set_at((x, y), (pixel[0], pixel[1], pixel[2]))

        self.canvas = new_canvas
        self.background_color = new_bg
        if self.current_color in [(0,0,0), (255,255,255)]:
            self.current_color = new_bg
        self.save_state()
        return new_bg

    def get_surface(self):
        return self.canvas
