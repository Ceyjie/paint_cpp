#!/usr/bin/env python3
"""
Pi Paint - Main Application
With in-app folder browser (no external windows)
"""

import pygame
import sys
import os
from datetime import datetime
import random
import string
from drawing import DrawingCanvas
from keyboard import VirtualKeyboard

# Colors
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 59, 48)
GREEN = (40, 205, 65)
BLUE = (0, 122, 255)
YELLOW = (255, 204, 0)
PURPLE = (175, 82, 222)
ORANGE = (255, 149, 0)
PINK = (255, 45, 85)
CYAN = (90, 200, 250)
LIGHT_GRAY = (229, 229, 234)
DARK_GRAY = (44, 44, 46)
TOOLBAR_BG = (240, 240, 245)
BORDER_COLOR = (180, 180, 185)
HIGHLIGHT = (180, 200, 255)


class PiPaint:
    def __init__(self):
        pygame.init()
        self.width = 1920
        self.height = 1080
        self.screen = pygame.display.set_mode((self.width, self.height),
                                              pygame.FULLSCREEN)
        pygame.display.set_caption("Pi Paint")
        pygame.mouse.set_visible(True)

        # Drawing canvas
        self.canvas = DrawingCanvas(self.width, self.height)

        # Color panel
        self.base_colors = [BLACK, RED, GREEN, BLUE,
                            YELLOW, PURPLE, ORANGE, PINK, CYAN]
        self.color_panel = self.base_colors.copy()
        self.update_color_panel()

        # Toolbar
        self.toolbar_height = 120
        self.toolbar_rect = pygame.Rect(0, 0, self.width, self.toolbar_height)
        self.toolbar_buttons = []
        self.create_toolbar()

        # Overlay state
        self.show_overlay = False
        self.overlay_type = None          # 'save' or 'load'
        self.overlay_files = []
        self.overlay_scroll = 0
        self.selected_file = None
        self.selected_index = -1
        self.files_per_page = 8
        self.filename_input = ""
        self.cursor_position = 0
        self.cursor_visible = True
        self.cursor_timer = 0
        self.typing_active = False

        # Folder browser state
        self.browsing_folder = False
        self.current_browse_path = ""
        self.subdirs = []
        self.browse_scroll = 0
        self.selected_subdir = -1

        # Scrollbar for file list / folder list
        self.scrollbar_dragging = False
        self.scrollbar_thumb_rect = None
        self.scrollbar_track_rect = None

        # Virtual keyboard
        self.font_small = pygame.font.Font(None, 28)
        self.font_medium = pygame.font.Font(None, 36)
        self.font_large = pygame.font.Font(None, 48)
        self.keyboard = VirtualKeyboard(self.font_medium)

        # Pen size slider
        self.slider_rect = pygame.Rect(self.width - 350, 45, 250, 20)
        self.slider_handle_rect = pygame.Rect(
            self.slider_rect.x, self.slider_rect.y - 8, 16, 36
        )
        self.update_slider_handle()
        self.dragging_slider = False

        # Message pop‑up
        self.message_text = ""
        self.message_time = 0

        # Clock
        self.clock = pygame.time.Clock()
        self.fps = 60
        self.running = True

        # Drawings directory
        self.default_dir = os.path.expanduser("~/pi-paint/drawings")
        self.current_dir = self.default_dir
        os.makedirs(self.current_dir, exist_ok=True)

        print("Pi Paint Started – in-app folder browser")

    # ------------------------------------------------------------------
    # Helper methods
    # ------------------------------------------------------------------
    def update_color_panel(self):
        bg = self.canvas.background_color
        self.color_panel[0] = WHITE if bg == BLACK else BLACK
        if self.canvas.current_color in (BLACK, WHITE):
            self.canvas.current_color = self.color_panel[0]

    def create_toolbar(self):
        # Color squares (top row)
        size = 45
        margin = 12
        y = 15
        x = 30
        for i in range(9):
            rect = pygame.Rect(x, y, size, size)
            self.toolbar_buttons.append({'rect': rect, 'type': 'color', 'index': i})
            x += size + margin

        # Text buttons (bottom row)
        btn_w, btn_h = 70, 40
        margin = 8
        y = 70
        x = 30
        texts = [
            ("Eraser", "eraser"),
            ("Bg", "background"),
            ("Undo", "undo"),
            ("Redo", "redo"),
            ("Clear", "clear"),
            ("Save", "save"),
            ("Load", "load"),
            ("Exit", "exit")
        ]
        for text, typ in texts:
            rect = pygame.Rect(x, y, btn_w, btn_h)
            self.toolbar_buttons.append({'rect': rect, 'type': typ, 'name': text})
            x += btn_w + margin

        self.separator_x = x + 30

    def update_slider_handle(self):
        size = self.canvas.pen_size
        min_s, max_s = self.canvas.min_size, self.canvas.max_size
        percent = (size - min_s) / (max_s - min_s)
        self.slider_handle_rect.x = self.slider_rect.x + percent * self.slider_rect.width - 8

    def show_message(self, text, duration=1.5):
        self.message_text = text
        self.message_time = pygame.time.get_ticks() + duration * 1000

    # ------------------------------------------------------------------
    # File overlay management
    # ------------------------------------------------------------------
    def show_save_overlay(self):
        self.show_overlay = True
        self.overlay_type = 'save'
        self.typing_active = True
        self.browsing_folder = False
        self.refresh_file_list()
        self.filename_input = self.generate_random_filename()
        self.cursor_position = len(self.filename_input)
        self.selected_file = None
        self.selected_index = -1
        self.keyboard.hide()
        self.cursor_timer = pygame.time.get_ticks()
        self.overlay_scroll = 0
        self.scrollbar_dragging = False
        win_w = 1000
        win_h = 550
        win_x = (self.width - win_w) // 2
        win_y = (self.height - win_h) // 2
        self.keyboard.x = win_x + (win_w - self.keyboard.width) // 2
        self.keyboard.y = win_y + 70

    def show_load_overlay(self):
        self.show_overlay = True
        self.overlay_type = 'load'
        self.typing_active = False
        self.browsing_folder = False
        self.refresh_file_list()
        self.filename_input = ""
        self.cursor_position = 0
        self.selected_file = None
        self.selected_index = -1
        self.keyboard.hide()
        self.cursor_timer = pygame.time.get_ticks()
        self.overlay_scroll = 0
        self.scrollbar_dragging = False
        win_w = 1000
        win_h = 550
        win_x = (self.width - win_w) // 2
        win_y = (self.height - win_h) // 2
        self.keyboard.x = win_x + (win_w - self.keyboard.width) // 2
        self.keyboard.y = win_y + 70

    def refresh_file_list(self):
        try:
            files = os.listdir(self.current_dir)
            self.overlay_files = [f for f in files if f.endswith('.png')]
            self.overlay_files.sort(reverse=True)
        except:
            self.overlay_files = []

    def generate_random_filename(self):
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        rand = ''.join(random.choices(string.ascii_lowercase + string.digits, k=4))
        return f"drawing_{ts}_{rand}.png"

    def save_current_drawing(self):
        if not self.filename_input:
            return
        fname = self.filename_input
        if not fname.endswith('.png'):
            fname += '.png'
        path = os.path.join(self.current_dir, fname)
        pygame.image.save(self.canvas.get_surface(), path)
        self.show_message(f"Saved: {fname}")
        self.show_overlay = False

    def load_selected_drawing(self):
        if not self.selected_file:
            return
        path = os.path.join(self.current_dir, self.selected_file)
        try:
            img = pygame.image.load(path)
            if img.get_size() != (self.width, self.height):
                img = pygame.transform.scale(img, (self.width, self.height))
            self.canvas.canvas.blit(img, (0, 0))
            self.canvas.save_state()
            self.show_message(f"Loaded: {self.selected_file}")
            self.show_overlay = False
        except:
            self.show_message("Load failed")

    # ------------------------------------------------------------------
    # Folder browser
    # ------------------------------------------------------------------
    def enter_folder_browser(self):
        self.browsing_folder = True
        self.current_browse_path = self.current_dir
        self.refresh_subdirs()
        self.browse_scroll = 0
        self.selected_subdir = -1
        self.typing_active = False

    def refresh_subdirs(self):
        """Get list of subdirectories in current_browse_path."""
        try:
            items = os.listdir(self.current_browse_path)
            self.subdirs = [d for d in items if os.path.isdir(os.path.join(self.current_browse_path, d))]
            self.subdirs.sort(key=str.lower)
        except:
            self.subdirs = []

    def go_up(self):
        parent = os.path.dirname(self.current_browse_path)
        if parent and parent != self.current_browse_path:
            self.current_browse_path = parent
            self.refresh_subdirs()
            self.browse_scroll = 0
            self.selected_subdir = -1

    def select_current_folder(self):
        """Set current_dir to the folder being browsed and return to normal mode."""
        self.current_dir = self.current_browse_path
        os.makedirs(self.current_dir, exist_ok=True)
        self.browsing_folder = False
        self.refresh_file_list()
        self.overlay_scroll = 0
        self.selected_file = None
        self.selected_index = -1

    # ------------------------------------------------------------------
    # Overlay drawing
    # ------------------------------------------------------------------
    def draw_overlay(self):
        if not self.show_overlay:
            return

        # Dim background
        dim = pygame.Surface((self.width, self.height))
        dim.set_alpha(220)
        dim.fill((240, 240, 245))
        self.screen.blit(dim, (0, 0))

        win_w = 1000
        win_h = 550
        win_x = (self.width - win_w) // 2
        win_y = (self.height - win_h) // 2

        # Main window
        pygame.draw.rect(self.screen, WHITE, (win_x, win_y, win_w, win_h))
        pygame.draw.rect(self.screen, BORDER_COLOR, (win_x, win_y, win_w, win_h), 3)

        # Title
        title = "Save Drawing" if self.overlay_type == 'save' else "Load Drawing"
        title_surf = self.font_large.render(title, True, DARK_GRAY)
        self.screen.blit(title_surf, (win_x + 20, win_y + 20))

        # ---------- Current folder and browse button ----------
        if self.browsing_folder:
            path_text = f"Browsing: {self.current_browse_path}"
        else:
            path_text = f"Folder: {self.current_dir}"
        if self.font_small.size(path_text)[0] > win_w - 150:
            short_path = "..." + path_text[-(win_w//10):]
            path_text = short_path
        self.screen.blit(self.font_small.render(path_text, True, DARK_GRAY),
                         (win_x + 20, win_y + 50))

        browse_btn = pygame.Rect(win_x + win_w - 120, win_y + 45, 100, 30)
        pygame.draw.rect(self.screen, LIGHT_GRAY, browse_btn)
        pygame.draw.rect(self.screen, BORDER_COLOR, browse_btn, 2)
        btn_text = "Back" if self.browsing_folder else "Browse"
        self.screen.blit(self.font_small.render(btn_text, True, DARK_GRAY),
                         (browse_btn.x + 20, browse_btn.y + 5))
        self.browse_btn_rect = browse_btn

        current_y = win_y + 90

        # ---------- Filename input (only in normal mode) ----------
        if not self.browsing_folder and self.overlay_type == 'save':
            self.screen.blit(
                self.font_medium.render("Filename:", True, DARK_GRAY),
                (win_x + 20, current_y)
            )
            current_y += 30

            box = pygame.Rect(win_x + 20, current_y, win_w - 40, 45)
            pygame.draw.rect(self.screen, LIGHT_GRAY, box)
            pygame.draw.rect(self.screen, BORDER_COLOR, box, 2)
            self.filename_box_rect = box
            current_y += 55

            if self.filename_input:
                display_text = self.filename_input
                while self.font_medium.size(display_text)[0] > box.width - 20 and len(display_text) > 5:
                    display_text = display_text[:-1]
                text_surf = self.font_medium.render(display_text, True, BLACK)
                self.screen.blit(text_surf, (box.x + 10, box.y + 10))
                if pygame.time.get_ticks() - self.cursor_timer > 500:
                    self.cursor_visible = not self.cursor_visible
                    self.cursor_timer = pygame.time.get_ticks()
                if self.cursor_visible:
                    cursor_x = box.x + 10 + text_surf.get_width()
                    pygame.draw.line(self.screen, BLACK,
                                     (cursor_x, box.y + 10),
                                     (cursor_x, box.y + 35), 2)
            else:
                if pygame.time.get_ticks() - self.cursor_timer > 500:
                    self.cursor_visible = not self.cursor_visible
                    self.cursor_timer = pygame.time.get_ticks()
                if self.cursor_visible:
                    pygame.draw.line(self.screen, BLACK,
                                     (box.x + 10, box.y + 10),
                                     (box.x + 10, box.y + 35), 2)

            # Buttons
            btn_w = 130
            btn_h = 35
            spacing = 10
            total_width = 3 * btn_w + 2 * spacing
            start_x = win_x + (win_w - total_width) // 2

            kb_btn = pygame.Rect(start_x, current_y, btn_w, btn_h)
            col = (200, 220, 255) if self.keyboard.visible else LIGHT_GRAY
            pygame.draw.rect(self.screen, col, kb_btn)
            pygame.draw.rect(self.screen, BORDER_COLOR, kb_btn, 2)
            self.screen.blit(self.font_small.render("Keyboard", True, DARK_GRAY),
                             (kb_btn.x + 10, kb_btn.y + 8))
            self.keyboard_btn_rect = kb_btn

            rand_btn = pygame.Rect(start_x + btn_w + spacing, current_y, btn_w, btn_h)
            pygame.draw.rect(self.screen, LIGHT_GRAY, rand_btn)
            pygame.draw.rect(self.screen, BORDER_COLOR, rand_btn, 1)
            self.screen.blit(self.font_small.render("Random", True, DARK_GRAY),
                             (rand_btn.x + 10, rand_btn.y + 8))
            self.random_btn_rect = rand_btn

            current_y += btn_h + 30
            list_y = current_y
        else:
            list_y = win_y + 120  # Load mode or browsing mode

        # ---------- File / Folder list ----------
        list_label = "Recent Files:" if not self.browsing_folder else "Folders:"
        self.screen.blit(self.font_medium.render(list_label, True, DARK_GRAY),
                         (win_x + 20, list_y))
        list_y += 40

        area_x = win_x + 20
        area_y = list_y
        area_w = win_w - 60
        area_h = 220
        if area_y + area_h + 60 > win_y + win_h:
            area_h = win_y + win_h - area_y - 60

        pygame.draw.rect(self.screen, LIGHT_GRAY,
                         (area_x, area_y, area_w, area_h))
        pygame.draw.rect(self.screen, BORDER_COLOR,
                         (area_x, area_y, area_w, area_h), 2)

        # Scrollbar track
        track_x = area_x + area_w + 5
        track_y = area_y
        track_w = 15
        track_h = area_h
        self.scrollbar_track_rect = pygame.Rect(track_x, track_y, track_w, track_h)
        pygame.draw.rect(self.screen, (200, 200, 200), self.scrollbar_track_rect)
        pygame.draw.rect(self.screen, BORDER_COLOR, self.scrollbar_track_rect, 1)

        self.file_rects = []  # reuse for folder items

        if self.browsing_folder:
            items = self.subdirs
            scroll = self.browse_scroll
            selected = self.selected_subdir
        else:
            items = self.overlay_files
            scroll = self.overlay_scroll
            selected = self.selected_index if self.selected_index >= 0 else -1

        start = scroll * self.files_per_page
        displayed = items[start:start + self.files_per_page]

        for i, name in enumerate(displayed):
            y = area_y + 5 + i * 35
            if y + 32 > area_y + area_h - 5:
                break
            rect = pygame.Rect(area_x + 2, y, area_w - 4, 32)
            # Highlight if selected
            idx_in_full = start + i
            if idx_in_full == selected:
                pygame.draw.rect(self.screen, HIGHLIGHT, rect)
            # Draw name
            display_name = name
            if self.browsing_folder:
                display_name = "📁 " + name
            text_surf = self.font_small.render(display_name, True, BLACK)
            self.screen.blit(text_surf, (area_x + 10, y + 5))
            # Store rect and index for click detection
            self.file_rects.append({'rect': rect, 'name': name, 'index': idx_in_full})

        # Scrollbar thumb
        total = len(items)
        if total > 0:
            visible = min(self.files_per_page, total)
            thumb_h = max(30, track_h * visible // total)
            max_scroll = max(0, (total - 1) // self.files_per_page)
            if max_scroll > 0:
                thumb_y = track_y + (scroll / max_scroll) * (track_h - thumb_h)
            else:
                thumb_y = track_y
            self.scrollbar_thumb_rect = pygame.Rect(track_x, int(thumb_y), track_w, thumb_h)
            pygame.draw.rect(self.screen, (100, 100, 100), self.scrollbar_thumb_rect)
            pygame.draw.rect(self.screen, DARK_GRAY, self.scrollbar_thumb_rect, 1)

        # ---------- Bottom buttons ----------
        btn_y = win_y + win_h - 60

        if self.browsing_folder:
            # Up button and Select button
            up_btn = pygame.Rect(win_x + win_w//2 - 160, btn_y, 100, 40)
            pygame.draw.rect(self.screen, LIGHT_GRAY, up_btn)
            pygame.draw.rect(self.screen, BORDER_COLOR, up_btn, 2)
            self.screen.blit(self.font_medium.render("Up", True, DARK_GRAY),
                             (up_btn.x + 30, up_btn.y + 10))
            self.up_btn_rect = up_btn

            select_btn = pygame.Rect(win_x + win_w//2 + 60, btn_y, 100, 40)
            pygame.draw.rect(self.screen, (100, 200, 100), select_btn)
            pygame.draw.rect(self.screen, BORDER_COLOR, select_btn, 2)
            self.screen.blit(self.font_medium.render("Select", True, WHITE),
                             (select_btn.x + 15, select_btn.y + 10))
            self.select_btn_rect = select_btn
        else:
            cancel_btn = pygame.Rect(win_x + win_w//2 - 110, btn_y, 100, 40)
            pygame.draw.rect(self.screen, LIGHT_GRAY, cancel_btn)
            pygame.draw.rect(self.screen, BORDER_COLOR, cancel_btn, 2)
            self.screen.blit(self.font_medium.render("Cancel", True, DARK_GRAY),
                             (cancel_btn.x + 15, cancel_btn.y + 10))
            self.cancel_btn_rect = cancel_btn

            action_btn = pygame.Rect(win_x + win_w//2 + 10, btn_y, 100, 40)
            action_color = (100, 200, 100) if self.overlay_type == 'save' else (100, 150, 255)
            pygame.draw.rect(self.screen, action_color, action_btn)
            pygame.draw.rect(self.screen, BORDER_COLOR, action_btn, 2)
            action_text = "Save" if self.overlay_type == 'save' else "Load"
            self.screen.blit(self.font_medium.render(action_text, True, WHITE),
                             (action_btn.x + 25, action_btn.y + 10))
            self.action_btn_rect = action_btn

        # ---------- Floating keyboard ----------
        if self.keyboard.visible:
            self.keyboard.draw(self.screen)

    # ------------------------------------------------------------------
    # Overlay click handling
    # ------------------------------------------------------------------
    def handle_overlay_click(self, pos):
        if not self.show_overlay:
            return False

        # 1) Keyboard title bar (dragging)
        if self.keyboard.handle_mouse_down(pos):
            return True

        # 2) Browse button
        if hasattr(self, 'browse_btn_rect') and self.browse_btn_rect.collidepoint(pos):
            if self.browsing_folder:
                # Exit folder browser back to normal mode without changing folder
                self.browsing_folder = False
            else:
                self.enter_folder_browser()
            return True

        # 3) Filename box (only if not browsing)
        if not self.browsing_folder and hasattr(self, 'filename_box_rect') and self.filename_box_rect.collidepoint(pos):
            self.typing_active = True
            if self.filename_input:
                click_x = pos[0] - (self.filename_box_rect.x + 10)
                total = 0
                for i, ch in enumerate(self.filename_input):
                    w = self.font_medium.render(ch, True, BLACK).get_width()
                    if click_x < total + w // 2:
                        self.cursor_position = i
                        break
                    total += w
                else:
                    self.cursor_position = len(self.filename_input)
            else:
                self.cursor_position = 0
            return True

        # 4) Keyboard keys
        key = self.keyboard.handle_click(pos)
        if key:
            self.process_keyboard_key(key)
            return True

        # 5) Keyboard toggle button (only in normal save mode)
        if not self.browsing_folder and self.overlay_type == 'save' and hasattr(self, 'keyboard_btn_rect'):
            if self.keyboard_btn_rect.collidepoint(pos):
                self.keyboard.toggle()
                return True

        # 6) Random name button (only in normal save mode)
        if not self.browsing_folder and self.overlay_type == 'save' and hasattr(self, 'random_btn_rect'):
            if self.random_btn_rect.collidepoint(pos):
                self.filename_input = self.generate_random_filename()
                self.cursor_position = len(self.filename_input)
                return True

        # 7) File/folder list selection
        if hasattr(self, 'file_rects'):
            for item in self.file_rects:
                if item['rect'].collidepoint(pos):
                    idx = item['index']
                    if self.browsing_folder:
                        # It's a folder – navigate into it
                        folder_name = item['name']
                        new_path = os.path.join(self.current_browse_path, folder_name)
                        if os.path.isdir(new_path):
                            self.current_browse_path = new_path
                            self.refresh_subdirs()
                            self.browse_scroll = 0
                            self.selected_subdir = -1
                        return True
                    else:
                        # It's a file – select it
                        self.selected_file = item['name']
                        try:
                            self.selected_index = self.overlay_files.index(self.selected_file)
                        except ValueError:
                            self.selected_index = -1
                        if self.overlay_type == 'load':
                            self.filename_input = item['name']
                            self.cursor_position = len(self.filename_input)
                        return True

        # 8) Scrollbar thumb (dragging)
        if self.scrollbar_thumb_rect and self.scrollbar_thumb_rect.collidepoint(pos):
            self.scrollbar_dragging = True
            return True

        # 9) Folder browser buttons
        if self.browsing_folder:
            if hasattr(self, 'up_btn_rect') and self.up_btn_rect.collidepoint(pos):
                self.go_up()
                return True
            if hasattr(self, 'select_btn_rect') and self.select_btn_rect.collidepoint(pos):
                self.select_current_folder()
                return True
        else:
            # Normal mode buttons
            if hasattr(self, 'cancel_btn_rect') and self.cancel_btn_rect.collidepoint(pos):
                self.show_overlay = False
                return True
            if hasattr(self, 'action_btn_rect') and self.action_btn_rect.collidepoint(pos):
                if self.overlay_type == 'save':
                    self.save_current_drawing()
                else:
                    self.load_selected_drawing()
                return True

        return False

    def process_keyboard_key(self, key):
        """Insert/delete characters based on virtual key press."""
        if key == 'Space':
            self._insert_char(' ')
        elif key == 'Bksp':
            if self.cursor_position > 0:
                self.filename_input = (self.filename_input[:self.cursor_position - 1] +
                                       self.filename_input[self.cursor_position:])
                self.cursor_position -= 1
        elif key == 'Enter':
            self.keyboard.hide()
        elif key == 'Caps':
            self.keyboard.caps_lock = not self.keyboard.caps_lock
        elif key == 'Shift':
            self.keyboard.caps_lock = not self.keyboard.caps_lock
        elif key in ('Tab', 'Ctrl', 'Alt'):
            pass
        elif key == '\\':
            self._insert_char('\\')
        elif key == '[':
            self._insert_char('[')
        elif key == ']':
            self._insert_char(']')
        elif key == ';':
            self._insert_char(';')
        elif key == "'":
            self._insert_char("'")
        elif key == ',':
            self._insert_char(',')
        elif key == '.':
            self._insert_char('.')
        elif key == '/':
            self._insert_char('/')
        elif key == '`':
            self._insert_char('`')
        elif key == '-':
            self._insert_char('-')
        elif key == '=':
            self._insert_char('=')
        else:
            char = key.upper() if self.keyboard.caps_lock and key.isalpha() else key
            self._insert_char(char)

    def _insert_char(self, ch):
        self.filename_input = (self.filename_input[:self.cursor_position] +
                               ch +
                               self.filename_input[self.cursor_position:])
        self.cursor_position += 1

    # ------------------------------------------------------------------
    # Toolbar click handling
    # ------------------------------------------------------------------
    def check_toolbar_click(self, pos):
        for btn in self.toolbar_buttons:
            if btn['rect'].collidepoint(pos):
                return btn
        return None

    def handle_toolbar(self, btn):
        typ = btn['type']
        if typ == 'color':
            idx = btn['index']
            self.canvas.set_color(self.color_panel[idx])
            self.show_message("Color selected")
        elif typ == 'eraser':
            self.canvas.toggle_eraser()
            self.show_message(f"Eraser: {'ON' if self.canvas.eraser_mode else 'OFF'}")
        elif typ == 'background':
            self.canvas.toggle_background()
            self.update_color_panel()
        elif typ == 'undo':
            self.canvas.undo()
        elif typ == 'redo':
            self.canvas.redo()
        elif typ == 'clear':
            self.canvas.clear_canvas()
            self.show_message("Canvas cleared")
        elif typ == 'save':
            self.show_save_overlay()
        elif typ == 'load':
            self.show_load_overlay()
        elif typ == 'exit':
            self.running = False

    # ------------------------------------------------------------------
    # Main loop
    # ------------------------------------------------------------------
    def run(self):
        self.update_color_panel()

        while self.running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    self.running = False

                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    pos = event.pos
                    if self.show_overlay:
                        if not self.handle_overlay_click(pos):
                            self.typing_active = False
                    elif self.slider_handle_rect.collidepoint(pos):
                        self.dragging_slider = True
                    else:
                        btn = self.check_toolbar_click(pos)
                        if btn:
                            self.handle_toolbar(btn)
                        elif pos[1] > self.toolbar_height:
                            self.canvas.start_stroke(pos)

                elif event.type == pygame.MOUSEBUTTONDOWN and event.button in (4, 5):
                    if self.show_overlay:
                        if self.browsing_folder:
                            total = len(self.subdirs)
                            scroll_var = 'browse_scroll'
                        else:
                            total = len(self.overlay_files)
                            scroll_var = 'overlay_scroll'
                        max_scroll = max(0, (total - 1) // self.files_per_page)
                        if event.button == 4:
                            setattr(self, scroll_var, max(0, getattr(self, scroll_var) - 1))
                        elif event.button == 5:
                            setattr(self, scroll_var, min(max_scroll, getattr(self, scroll_var) + 1))

                elif event.type == pygame.MOUSEMOTION:
                    if self.keyboard.dragging:
                        self.keyboard.handle_mouse_move(event.pos, self.width, self.height)
                    elif self.dragging_slider:
                        rel_x = event.pos[0] - self.slider_rect.x
                        rel_x = max(0, min(rel_x, self.slider_rect.width))
                        percent = rel_x / self.slider_rect.width
                        size = int(self.canvas.min_size +
                                   percent * (self.canvas.max_size - self.canvas.min_size))
                        self.canvas.set_size(size)
                        self.update_slider_handle()
                    elif self.scrollbar_dragging and self.scrollbar_track_rect:
                        _, y = event.pos
                        track_top = self.scrollbar_track_rect.top
                        track_bottom = self.scrollbar_track_rect.bottom
                        rel_y = max(track_top, min(y, track_bottom)) - track_top
                        if self.browsing_folder:
                            total = len(self.subdirs)
                            scroll_var = 'browse_scroll'
                        else:
                            total = len(self.overlay_files)
                            scroll_var = 'overlay_scroll'
                        max_scroll = max(0, (total - 1) // self.files_per_page)
                        if max_scroll > 0:
                            scroll = int((rel_y / self.scrollbar_track_rect.height) * max_scroll)
                            setattr(self, scroll_var, max(0, min(scroll, max_scroll)))
                    elif self.canvas.last_pos and not self.show_overlay:
                        if event.pos[1] > self.toolbar_height:
                            self.canvas.continue_stroke(event.pos)

                elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                    self.dragging_slider = False
                    self.scrollbar_dragging = False
                    self.keyboard.handle_mouse_up()
                    self.canvas.end_stroke()

                elif event.type == pygame.KEYDOWN:
                    # Global shortcuts – only if not typing
                    if not self.typing_active:
                        if event.key == pygame.K_ESCAPE:
                            if self.show_overlay:
                                if self.keyboard.visible:
                                    self.keyboard.hide()
                                else:
                                    self.show_overlay = False
                            else:
                                self.running = False
                        elif event.key == pygame.K_e:
                            self.canvas.toggle_eraser()
                        elif event.key == pygame.K_b:
                            self.canvas.toggle_background()
                            self.update_color_panel()
                        elif event.key == pygame.K_s and pygame.key.get_mods() & pygame.KMOD_CTRL:
                            self.show_save_overlay()
                        elif event.key == pygame.K_o and pygame.key.get_mods() & pygame.KMOD_CTRL:
                            self.show_load_overlay()
                    else:
                        # Typing active – handle text input
                        if self.overlay_type == 'save' and not self.browsing_folder:
                            if event.key == pygame.K_BACKSPACE:
                                if self.cursor_position > 0:
                                    self.filename_input = (self.filename_input[:self.cursor_position - 1] +
                                                           self.filename_input[self.cursor_position:])
                                    self.cursor_position -= 1
                            elif event.key == pygame.K_DELETE:
                                if self.cursor_position < len(self.filename_input):
                                    self.filename_input = (self.filename_input[:self.cursor_position] +
                                                           self.filename_input[self.cursor_position + 1:])
                            elif event.key == pygame.K_LEFT:
                                self.cursor_position = max(0, self.cursor_position - 1)
                            elif event.key == pygame.K_RIGHT:
                                self.cursor_position = min(len(self.filename_input),
                                                            self.cursor_position + 1)
                            elif event.key == pygame.K_HOME:
                                self.cursor_position = 0
                            elif event.key == pygame.K_END:
                                self.cursor_position = len(self.filename_input)
                            elif event.key == pygame.K_RETURN:
                                self.save_current_drawing()
                            else:
                                if event.unicode and event.unicode.isprintable():
                                    self._insert_char(event.unicode)

                    # Arrow key navigation for lists (even when typing)
                    if self.show_overlay:
                        if self.browsing_folder:
                            items = self.subdirs
                            selected_attr = 'selected_subdir'
                            scroll_attr = 'browse_scroll'
                        else:
                            items = self.overlay_files
                            selected_attr = 'selected_index'
                            scroll_attr = 'overlay_scroll'
                        total = len(items)
                        if event.key == pygame.K_UP:
                            current = getattr(self, selected_attr)
                            if current > 0:
                                setattr(self, selected_attr, current - 1)
                            elif current == -1 and total > 0:
                                setattr(self, selected_attr, 0)
                            else:
                                setattr(self, selected_attr, 0)
                            self._ensure_visible(selected_attr, scroll_attr)
                        elif event.key == pygame.K_DOWN:
                            current = getattr(self, selected_attr)
                            if current < total - 1:
                                setattr(self, selected_attr, current + 1)
                            elif current == -1 and total > 0:
                                setattr(self, selected_attr, 0)
                            else:
                                setattr(self, selected_attr, total - 1)
                            self._ensure_visible(selected_attr, scroll_attr)
                        elif event.key == pygame.K_HOME and total > 0:
                            setattr(self, selected_attr, 0)
                            self._ensure_visible(selected_attr, scroll_attr)
                        elif event.key == pygame.K_END and total > 0:
                            setattr(self, selected_attr, total - 1)
                            self._ensure_visible(selected_attr, scroll_attr)
                        elif event.key == pygame.K_PAGEUP:
                            setattr(self, scroll_attr, max(0, getattr(self, scroll_attr) - 1))
                        elif event.key == pygame.K_PAGEDOWN:
                            max_scroll = max(0, (total - 1) // self.files_per_page)
                            setattr(self, scroll_attr, min(max_scroll, getattr(self, scroll_attr) + 1))

            # ----- Drawing -----
            self.screen.blit(self.canvas.get_surface(), (0, 0))

            # Toolbar
            pygame.draw.rect(self.screen, TOOLBAR_BG, self.toolbar_rect)
            pygame.draw.line(self.screen, BORDER_COLOR,
                             (0, self.toolbar_height),
                             (self.width, self.toolbar_height), 2)
            pygame.draw.line(self.screen, BORDER_COLOR,
                             (self.separator_x, 20),
                             (self.separator_x, self.toolbar_height - 20), 2)

            color_idx = 0
            for btn in self.toolbar_buttons:
                if btn['type'] == 'color':
                    col = self.color_panel[btn['index']]
                    if col == self.canvas.current_color and not self.canvas.eraser_mode:
                        pygame.draw.rect(self.screen, BLUE,
                                         btn['rect'].inflate(6, 6), 3)
                    pygame.draw.rect(self.screen, col, btn['rect'])
                    pygame.draw.rect(self.screen, DARK_GRAY, btn['rect'], 1)
                elif btn['type'] != 'color':
                    btn_color = LIGHT_GRAY
                    if btn['type'] == 'eraser' and self.canvas.eraser_mode:
                        btn_color = (200, 220, 255)
                    pygame.draw.rect(self.screen, btn_color, btn['rect'])
                    pygame.draw.rect(self.screen, BORDER_COLOR, btn['rect'], 1)
                    text = self.font_small.render(btn['name'], True, DARK_GRAY)
                    text_rect = text.get_rect(center=btn['rect'].center)
                    self.screen.blit(text, text_rect)

            # Slider
            pygame.draw.rect(self.screen, LIGHT_GRAY, self.slider_rect)
            pygame.draw.rect(self.screen, BORDER_COLOR, self.slider_rect, 2)
            fill_w = int((self.canvas.pen_size - self.canvas.min_size) /
                         (self.canvas.max_size - self.canvas.min_size) *
                         self.slider_rect.width)
            if fill_w > 0:
                fill_rect = pygame.Rect(self.slider_rect.x, self.slider_rect.y,
                                        fill_w, self.slider_rect.height)
                pygame.draw.rect(self.screen, BLUE, fill_rect)
            pygame.draw.rect(self.screen, DARK_GRAY, self.slider_handle_rect)
            pygame.draw.rect(self.screen, BLACK, self.slider_handle_rect, 2)

            size_label = self.font_small.render(f"{self.canvas.pen_size}px", True, DARK_GRAY)
            self.screen.blit(size_label, (self.slider_rect.x - 60, self.slider_rect.y - 5))

            if self.show_overlay:
                self.draw_overlay()

            # Bottom status
            y = self.height - 40
            if self.canvas.eraser_mode:
                tool = f"Eraser • Size: {self.canvas.pen_size}px"
                preview = self.canvas.background_color
            else:
                tool = f"Pen • Size: {self.canvas.pen_size}px"
                preview = self.canvas.current_color
            pygame.draw.circle(self.screen, preview, (30, y + 10), 15)
            pygame.draw.circle(self.screen, DARK_GRAY, (30, y + 10), 15, 2)
            self.screen.blit(self.font_small.render(tool, True, DARK_GRAY), (55, y))

            if self.message_text and pygame.time.get_ticks() < self.message_time:
                msg = self.font_medium.render(self.message_text, True, WHITE)
                bg = pygame.Surface((msg.get_width() + 30, msg.get_height() + 15))
                bg.set_alpha(200)
                bg.fill(BLACK)
                msg_rect = msg.get_rect(center=(self.width // 2, self.height // 2))
                self.screen.blit(bg, (msg_rect.x - 15, msg_rect.y - 7))
                self.screen.blit(msg, msg_rect)

            pygame.display.flip()
            self.clock.tick(self.fps)

        pygame.quit()
        sys.exit()

    def _ensure_visible(self, selected_attr, scroll_attr):
        """Ensure selected item is within the visible area."""
        selected = getattr(self, selected_attr)
        scroll = getattr(self, scroll_attr)
        page = selected // self.files_per_page
        if page < scroll:
            setattr(self, scroll_attr, page)
        elif page > scroll:
            if self.browsing_folder:
                total = len(self.subdirs)
            else:
                total = len(self.overlay_files)
            max_scroll = max(0, (total - 1) // self.files_per_page)
            setattr(self, scroll_attr, min(page, max_scroll))


if __name__ == '__main__':
    app = PiPaint()
    app.run()
