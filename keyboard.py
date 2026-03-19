import pygame

class VirtualKeyboard:
    def __init__(self, font):
        self.font = font
        self.visible = False
        self.x = 0
        self.y = 0
        self.dragging = False
        self.drag_offset_x = 0
        self.drag_offset_y = 0
        self.key_rects = []
        self.caps_lock = False
        self.width = 900
        self.height = 280
        self.title_bar_rect = None

    def show(self, screen_width, screen_height):
        self.visible = True
        self.x = (screen_width - self.width) // 2
        self.y = (screen_height - 650) // 2 + 280
        self.caps_lock = False

    def hide(self):
        self.visible = False

    def toggle(self):
        self.visible = not self.visible

    def handle_mouse_down(self, pos):
        if not self.visible or not self.title_bar_rect:
            return False
        if self.title_bar_rect.collidepoint(pos):
            self.dragging = True
            self.drag_offset_x = pos[0] - self.x
            self.drag_offset_y = pos[1] - self.y
            return True
        return False

    def handle_mouse_up(self):
        self.dragging = False

    def handle_mouse_move(self, pos, screen_width, screen_height):
        if self.dragging:
            self.x = pos[0] - self.drag_offset_x
            self.y = pos[1] - self.drag_offset_y
            self.x = max(0, min(self.x, screen_width - self.width))
            self.y = max(0, min(self.y, screen_height - self.height))

    def handle_click(self, pos):
        if not self.visible:
            return None
        for rect, key in self.key_rects:
            if rect.collidepoint(pos):
                return key
        return None

    def draw(self, screen):
        if not self.visible:
            return

        x, y = self.x, self.y
        # Title bar
        self.title_bar_rect = pygame.Rect(x, y - 25, self.width, 25)
        pygame.draw.rect(screen, (180, 180, 200), self.title_bar_rect)
        pygame.draw.rect(screen, (180, 180, 185), self.title_bar_rect, 2)
        title_text = self.font.render("Keyboard (drag here)", True, (0, 0, 0))
        screen.blit(title_text, (x + 10, y - 20))

        # Background
        pygame.draw.rect(screen, (220, 220, 225), (x, y, self.width, self.height))
        pygame.draw.rect(screen, (180, 180, 185), (x, y, self.width, self.height), 3)

        # Layout
        keys = self._create_layout()
        self.key_rects = []
        base_w = 50
        h = 40
        margin = 5

        for row_idx, row in enumerate(keys):
            row_width = 0
            key_widths = []
            for key in row:
                if key in ('Tab', 'Caps', 'Enter', 'Shift', 'Bksp'):
                    w = base_w * 1.5
                elif key == 'Space':
                    w = base_w * 6
                elif key in ('Ctrl', 'Alt'):
                    w = base_w * 1.3
                else:
                    w = base_w
                key_widths.append(w)
                row_width += w + margin
            row_width -= margin
            x_offset = x + (self.width - row_width) // 2
            y_offset = y + 10 + row_idx * (h + margin)

            for col_idx, key in enumerate(row):
                w = key_widths[col_idx]
                rect = pygame.Rect(x_offset, y_offset, w, h)

                display = key
                if key == 'Space':
                    display = 'Space'
                elif key == 'Bksp':
                    display = 'Bksp'
                elif len(key) == 1 and key.isalpha() and self.caps_lock:
                    display = key.upper()

                if key in ('Tab', 'Caps', 'Enter', 'Shift', 'Ctrl', 'Alt', 'Bksp'):
                    color = (180, 180, 190)
                else:
                    color = (255, 255, 255)

                pygame.draw.rect(screen, color, rect)
                pygame.draw.rect(screen, (44, 44, 46), rect, 2)

                text_surf = self.font.render(display, True, (0, 0, 0))
                text_rect = text_surf.get_rect(center=rect.center)
                screen.blit(text_surf, text_rect)

                self.key_rects.append((rect, key))
                x_offset += w + margin

    def _create_layout(self):
        return [
            ['`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 'Bksp'],
            ['Tab', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'],
            ['Caps', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', "'", 'Enter'],
            ['Shift', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 'Shift'],
            ['Ctrl', 'Alt', 'Space', 'Alt', 'Ctrl']
        ]
