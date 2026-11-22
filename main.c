// My femboy - уютная игра про ухаживание за котиком для PSP
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspge.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("MyFemboy", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define W 480
#define H 272
#define BUFW 512
#define FBSZ (BUFW * H * 4)
#define RGB(r, g, b) (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

// ---------- выход по HOME ----------
static int running = 1;
static int exit_cb(int a, int b, void *c) { running = 0; return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
}

// ---------- рисование ----------
static u32 *fb;

static void px(int x, int y, u32 c) {
    if (x >= 0 && x < W && y >= 0 && y < H) fb[y * BUFW + x] = c;
}
static void rect(int x, int y, int w, int h, u32 c) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < W) fb[j * BUFW + i] = c;
    }
}
static void rrect(int x, int y, int w, int h, u32 c) {
    rect(x + 2, y, w - 4, h, c);
    rect(x, y + 2, w, h - 4, c);
    rect(x + 1, y + 1, w - 2, h - 2, c);
}
static void ell(int cx, int cy, int rx, int ry, u32 c) {
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    for (int j = -ry; j <= ry; j++) {
        float t = 1.0f - (float)(j * j) / (float)(ry * ry);
        if (t < 0) t = 0;
        int hw = (int)(rx * sqrtf(t));
        rect(cx - hw, cy + j, hw * 2 + 1, 1, c);
    }
}
static int edge(int ax, int ay, int bx, int by, int x, int y) {
    return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}
static void tri(int x0, int y0, int x1, int y1, int x2, int y2, u32 c) {
    int minx = x0, maxx = x0, miny = y0, maxy = y0;
    if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
    if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;
    if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
    if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            int a = edge(x0, y0, x1, y1, x, y);
            int b = edge(x1, y1, x2, y2, x, y);
            int d = edge(x2, y2, x0, y0, x, y);
            if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) px(x, y, c);
        }
}
static void line(int x0, int y0, int x1, int y1, int t, u32 c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        rect(x0, y0, t, t, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void text(int x, int y, u32 col, const char *s) {
    pspDebugScreenSetTextColor(col);
    pspDebugScreenSetXY(x / 7, y / 8);
    pspDebugScreenPuts(s);
}

// ---------- цвета ----------
#define OUTL RGB(95, 70, 85)
#define SKIN RGB(252, 244, 238)
#define HAIR RGB(245, 240, 232)
#define HAIRSH RGB(222, 210, 205)
#define EARIN RGB(247, 190, 200)
#define BLUSH RGB(255, 165, 175)
#define PINK RGB(255, 205, 212)
#define PINK2 RGB(255, 190, 200)
#define CORAL RGB(240, 110, 110)
#define MINT RGB(150, 225, 190)
#define WHITE RGB(255, 255, 255)

static void orect(int x, int y, int w, int h, u32 c) {
    rrect(x - 1, y - 1, w + 2, h + 2, OUTL);
    rrect(x, y, w, h, c);
}
static void oell(int cx, int cy, int rx, int ry, u32 c) {
    ell(cx, cy, rx + 1, ry + 1, OUTL);
    ell(cx, cy, rx, ry, c);
}

// ---------- состояние игры ----------
enum { M_PET, M_FEED, M_DRINK, M_SLEEP, M_COUNT };
static const char *menu_names[M_COUNT] = {"PET", "FEED", "DRINK", "SLEEP"};

#define NFOOD 4
static const char *food_names[NFOOD] = {"Toast", "Strawberry", "Cake", "Fries"};
static const int food_cost[NFOOD] = {5, 3, 10, 8};
static const float food_gain[NFOOD] = {25, 15, 40, 30};

#define NOUT 4
static const char *outfit_names[NOUT] = {"Overalls", "Gray tee", "Maid dress", "White dress"};
static const int outfit_price[NOUT] = {0, 30, 60, 90};

static float hunger = 80, thirst = 80, energy = 80, love = 60;
static int cookies = 100, gems = 0;
static int room = 0, outfit = 0, menu = 0, food = 0;
static int owned[NOUT] = {1, 0, 0, 0};
static int sleeping = 0;
static int eat_t = 0, drink_t = 0, mood_t = 0;
static int strokes = 0, lastdir = 0;
static int frame = 0;
static int title = 1;
static char msg[48] = "";
static int msg_t = 0;

typedef struct { float x, y, vy; int life; } Heart;
static Heart hearts[16];

static void say(const char *s) {
    strncpy(msg, s, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
    msg_t = 120;
}
static void spawn_heart(int cx, int cy) {
    for (int i = 0; i < 16; i++)
        if (hearts[i].life <= 0) {
            hearts[i].x = cx + (rand() % 50) - 25;
            hearts[i].y = cy + (rand() % 20);
            hearts[i].vy = -0.6f - (rand() % 10) / 10.0f;
            hearts[i].life = 70;
            return;
        }
}
static float clampf(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

// что хочет котик: 0 фри, 1 попить, 2 спать, 3 погладить, -1 ничего
static int want(void) {
    float m = hunger; int w = 0;
    if (thirst < m) { m = thirst; w = 1; }
    if (energy < m) { m = energy; w = 2; }
    if (love < m) { m = love; w = 3; }
    return m < 45 ? w : -1;
}

// ---------- иконки ----------
static void icon(int type, int x, int y, int s) {
    // x,y - центр, s - половина размера
    switch (type) {
    case 0: // тост
        orect(x - s, y - s, s * 2, s * 2, RGB(240, 200, 130));
        ell(x, y, s / 2, s / 2, RGB(255, 245, 200));
        ell(x, y, s / 4 + 1, s / 4 + 1, RGB(255, 190, 40));
        break;
    case 1: // клубника
        ell(x, y + 1, s - 1, s, RGB(235, 60, 70));
        tri(x - s / 2, y - s, x + s / 2, y - s, x, y - s / 3, RGB(70, 170, 80));
        break;
    case 2: // торт
        tri(x - s, y + s, x + s, y + s, x + s, y - s, RGB(250, 150, 160));
        rect(x - s, y + s - 3, s * 2, 3, RGB(255, 235, 200));
        ell(x + s - 3, y - s + 3, 3, 3, RGB(230, 40, 60));
        break;
    case 3: // фри
        for (int i = 0; i < 4; i++)
            rect(x - s + 2 + i * (s / 2), y - s - (i % 2) * 3, 4, s, RGB(255, 210, 70));
        orect(x - s, y - 1, s * 2, s + 2, RGB(235, 60, 70));
        break;
    case 4: // кружка/стакан
        orect(x - s + 2, y - s, s * 2 - 4, s * 2, RGB(170, 220, 240));
        rect(x - s + 4, y - s + 3, s * 2 - 8, 4, WHITE);
        break;
    case 5: // подушка
        orect(x - s, y - s + 2, s * 2, s * 2 - 4, RGB(215, 195, 240));
        ell(x, y, 3, 3, RGB(250, 170, 210));
        break;
    case 6: // лапка с сердцем
        ell(x, y + 3, s / 2 + 2, s / 2, WHITE);
        ell(x - s / 2 - 1, y - s / 3, 3, 4, WHITE);
        ell(x + s / 2 + 1, y - s / 3, 3, 4, WHITE);
        ell(x - 3, y - s / 2 - 3, 3, 4, WHITE);
        ell(x + 3, y - s / 2 - 3, 3, 4, WHITE);
        ell(x - 2, y + 3, 2, 2, RGB(250, 120, 160));
        ell(x + 2, y + 3, 2, 2, RGB(250, 120, 160));
        break;
    }
}
static void heart(int x, int y, int s, u32 c) {
    ell(x - s / 2, y, s / 2 + 1, s / 2 + 1, c);
    ell(x + s / 2, y, s / 2 + 1, s / 2 + 1, c);
    tri(x - s - 1, y + 1, x + s + 1, y + 1, x, y + s + 3, c);
}

// ---------- комната ----------
static void draw_room_back(void) {
    if (room == 0) {
        rect(0, 0, W, 205, PINK);
        for (int x = 0; x < W; x += 28) rect(x, 0, 12, 205, RGB(255, 218, 222));
        rect(0, 205, W, 67, RGB(252, 232, 228));
        for (int x = 0; x < W; x += 60) rect(x, 205, 2, 67, RGB(240, 210, 205));
        // окно-картина
        orect(300, 40, 70, 60, RGB(200, 235, 250));
        // телевизор
        orect(15, 122, 80, 56, RGB(110, 120, 120));
        rect(22, 129, 66, 40, RGB(190, 230, 245));
        rect(8, 178, 96, 24, RGB(140, 200, 175));
        // диван
        orect(370, 110, 100, 90, RGB(245, 110, 110));
        orect(355, 165, 120, 40, RGB(235, 90, 95));
        // ковёр
        ell(W / 2, 212, 105, 16, RGB(250, 150, 160));
    } else {
        rect(0, 0, W, 205, RGB(255, 200, 205));
        for (int y = 40; y < 150; y += 20) rect(0, y, W, 2, RGB(250, 175, 185));
        // полка
        rect(100, 35, 260, 5, RGB(190, 90, 90));
        orect(130, 18, 40, 17, RGB(170, 215, 195));
        orect(220, 40, 24, 36, RGB(250, 150, 90));
        orect(260, 40, 24, 36, RGB(240, 90, 100));
        // холодильник
        orect(8, 20, 62, 185, RGB(248, 248, 248));
        rect(10, 80, 58, 2, RGB(200, 200, 200));
        // плита
        orect(330, 120, 120, 85, RGB(95, 95, 100));
        orect(350, 140, 80, 45, RGB(80, 80, 85));
        orect(375, 148, 30, 30, RGB(250, 160, 170));
        // шкафчики
        orect(80, 135, 235, 70, RGB(150, 215, 185));
        rect(80, 130, 235, 8, RGB(255, 235, 235));
    }
}
static void draw_room_front(void) {
    if (room == 1) {
        orect(-5, 192, W + 10, 90, RGB(245, 130, 130));
        rect(0, 196, W, 4, RGB(255, 170, 170));
    }
}

// ---------- котик ----------
static void draw_cat(int cx, int dx, int mood) {
    int hx = cx + dx;
    int wag = (int)(sinf(frame * 0.12f) * 8.0f);
    if (mood == 1) wag = (int)(sinf(frame * 0.35f) * 14.0f);

    // хвост
    line(cx + 24, 170, cx + 44 + wag, 150, 5, OUTL);
    line(cx + 24, 170, cx + 44 + wag, 150, 3, HAIR);

    // ноги
    orect(cx - 21, 168, 15, 46, SKIN);
    orect(cx + 6, 168, 15, 46, SKIN);
    oell(cx - 14, 214, 10, 4, SKIN);
    oell(cx + 14, 214, 10, 4, SKIN);

    // руки
    orect(cx - 42, 122, 11, 48, SKIN);
    orect(cx + 31, 122, 11, 48, SKIN);

    // одежда
    switch (outfit) {
    case 0: // комбинезон
        orect(cx - 26, 116, 52, 50, RGB(40, 40, 50));
        orect(cx - 25, 138, 50, 40, RGB(100, 150, 190));
        orect(cx - 25, 125, 8, 20, RGB(100, 150, 190));
        orect(cx + 17, 125, 8, 20, RGB(100, 150, 190));
        ell(cx, 152, 6, 6, WHITE);
        ell(cx, 152, 2, 2, RGB(255, 210, 80));
        break;
    case 1: // серая футболка
        orect(cx - 28, 116, 56, 62, RGB(185, 170, 170));
        orect(cx - 42, 118, 14, 20, RGB(185, 170, 170));
        orect(cx + 28, 118, 14, 20, RGB(185, 170, 170));
        break;
    case 2: // платье горничной
        orect(cx - 26, 116, 52, 44, RGB(45, 40, 50));
        orect(cx - 36, 158, 72, 26, RGB(45, 40, 50));
        rect(cx - 36, 180, 72, 4, WHITE);
        orect(cx - 16, 120, 32, 60, WHITE);
        tri(cx - 14, 126, cx - 2, 130, cx - 14, 138, RGB(30, 25, 35));
        tri(cx + 14, 126, cx + 2, 130, cx + 14, 138, RGB(30, 25, 35));
        ell(cx, 130, 3, 3, RGB(30, 25, 35));
        break;
    case 3: // белое платье
        orect(cx - 26, 116, 52, 46, RGB(255, 250, 250));
        orect(cx - 38, 158, 76, 50, RGB(250, 244, 246));
        rect(cx - 38, 200, 76, 4, RGB(235, 225, 230));
        oell(cx - 38, 124, 10, 10, RGB(255, 250, 250));
        oell(cx + 38, 124, 10, 10, RGB(255, 250, 250));
        break;
    }

    // воротник/шея
    rect(cx - 6 + dx / 2, 110, 12, 8, SKIN);

    // уши
    tri(hx - 40, 64, hx - 34, 22, hx - 12, 48, OUTL);
    tri(hx - 36, 60, hx - 33, 28, hx - 16, 48, HAIR);
    tri(hx - 31, 52, hx - 30, 36, hx - 21, 48, EARIN);
    tri(hx + 40, 64, hx + 34, 22, hx + 12, 48, OUTL);
    tri(hx + 36, 60, hx + 33, 28, hx + 16, 48, HAIR);
    tri(hx + 31, 52, hx + 30, 36, hx + 21, 48, EARIN);

    // голова
    oell(hx, 74, 40, 38, HAIR);
    oell(hx, 90, 33, 28, SKIN);
    // чёлка
    ell(hx, 62, 36, 18, HAIR);
    tri(hx - 36, 62, hx - 8, 62, hx - 20, 88, HAIR);
    tri(hx + 36, 62, hx + 8, 62, hx + 24, 82, HAIR);
    tri(hx - 6, 62, hx + 10, 62, hx + 2, 80, HAIRSH);
    rect(hx - 40, 70, 8, 34, HAIR);
    rect(hx + 32, 70, 8, 34, HAIR);

    // горничная: повязка
    if (outfit == 2) {
        orect(hx - 20, 44, 40, 8, RGB(40, 35, 45));
        tri(hx - 6, 40, hx + 6, 40, hx, 50, RGB(255, 255, 255));
    }

    // лицо
    int ey = 94;
    ell(hx - 20, ey + 9, 6, 3, BLUSH);
    ell(hx + 20, ey + 9, 6, 3, BLUSH);
    if (mood == 1) { // счастье ^ ^
        line(hx - 18, ey + 3, hx - 13, ey - 3, 2, OUTL);
        line(hx - 13, ey - 3, hx - 8, ey + 3, 2, OUTL);
        line(hx + 8, ey + 3, hx + 13, ey - 3, 2, OUTL);
        line(hx + 13, ey - 3, hx + 18, ey + 3, 2, OUTL);
        ell(hx, ey + 14, 5, 4, RGB(200, 90, 110));
        rect(hx - 5, ey + 10, 11, 3, SKIN);
    } else if (mood == 3) { // спит
        line(hx - 18, ey + 1, hx - 8, ey + 1, 2, OUTL);
        line(hx + 8, ey + 1, hx + 18, ey + 1, 2, OUTL);
        line(hx - 3, ey + 14, hx + 3, ey + 14, 2, OUTL);
    } else {
        int sad = (mood == 2);
        ell(hx - 13, ey, 4, 5, RGB(70, 90, 80));
        ell(hx + 13, ey, 4, 5, RGB(70, 90, 80));
        ell(hx - 14, ey - 2, 1, 1, WHITE);
        ell(hx + 12, ey - 2, 1, 1, WHITE);
        if (sad) {
            line(hx - 20, ey - 9, hx - 8, ey - 6, 1, OUTL);
            line(hx + 20, ey - 9, hx + 8, ey - 6, 1, OUTL);
            line(hx - 4, ey + 16, hx + 4, ey + 16, 2, OUTL);
        } else {
            line(hx - 3, ey + 14, hx, ey + 15, 1, OUTL);
            line(hx, ey + 15, hx + 3, ey + 14, 1, OUTL);
        }
        if (eat_t > 0 || drink_t > 0) ell(hx, ey + 14, 5, 3 + (frame / 6) % 3, RGB(200, 90, 110));
    }
}

// ---------- интерфейс ----------
static void pill(int x, int y, int w, u32 c) {
    orect(x, y, w, 18, c);
}
static void bar(int x, int y, const char *label, float v, u32 c) {
    text(x, y, RGB(120, 60, 80), label);
    rect(x + 42, y, 52, 8, OUTL);
    rect(x + 43, y + 1, 50, 6, RGB(255, 240, 240));
    rect(x + 43, y + 1, (int)(v * 0.5f), 6, c);
}
static void draw_ui(int wantt) {
    // верхние плашки
    pill(8, 6, 70, WHITE);
    heart(22, 14, 5, CORAL);
    char b[32];
    snprintf(b, sizeof(b), "%d", gems);
    text(36, 11, OUTL, b);
    pill(86, 6, 80, WHITE);
    ell(100, 15, 6, 6, RGB(220, 160, 90));
    snprintf(b, sizeof(b), "%d", cookies);
    text(112, 11, OUTL, b);

    // потребности
    bar(8, 32, "FOOD", hunger, RGB(250, 160, 80));
    bar(8, 44, "DRNK", thirst, RGB(100, 190, 240));
    bar(8, 56, "ZZZ", energy, RGB(180, 150, 240));
    bar(8, 68, "LOVE", love, RGB(250, 110, 150));

    // пузырь желания
    if (wantt >= 0 && !sleeping) {
        int bx = 160, by = 56;
        ell(bx + 18, by + 20, 6, 5, WHITE);
        ell(bx + 8, by + 32, 3, 3, WHITE);
        oell(bx + 40, by - 2, 30, 24, WHITE);
        int ic[4] = {3, 4, 5, 6};
        if (wantt == 0) ic[0] = 3;
        icon(ic[wantt], bx + 40, by - 2, 12);
    }

    // кнопки меню
    for (int i = 0; i < M_COUNT; i++) {
        int x = 170 + i * 36 - 0, y = 228;
        int sel = (i == menu);
        int s = sel ? 17 : 15;
        u32 bg = sel ? MINT : RGB(215, 200, 205);
        orect(x - s + 8, y - s + 8 - (sel ? 3 : 0), s * 2, s * 2, bg);
        int ic[4] = {6, food == 3 ? 3 : food, 4, 5};
        icon(ic[i], x + 8, y + 8 - (sel ? 3 : 0), 10);
    }
    // подсказка выбранного
    if (menu == M_FEED) {
        snprintf(b, sizeof(b), "%s (%d)  UP/DOWN", food_names[food], food_cost[food]);
        text(150, 205, OUTL, b);
    } else if (menu == M_PET) {
        text(130, 205, OUTL, "Wiggle LEFT STICK left-right!");
    } else if (menu == M_DRINK) {
        text(170, 205, OUTL, "X - give water (2)");
    } else {
        text(165, 205, OUTL, sleeping ? "X - wake up" : "X - go to sleep");
    }
    text(6, 262, RGB(120, 60, 80), "<>: choose  X: use  []: outfit  /\\: room");

    if (msg_t > 0) {
        int w = (int)strlen(msg) * 7 + 12;
        orect(W / 2 - w / 2, 100, w, 18, WHITE);
        text(W / 2 - w / 2 + 6, 105, OUTL, msg);
    }
}


// ---------- заставка ----------
static const char *glyph(char ch) {
    switch (ch) {
    case 'M': return "10001110111010110001" "10001";
    case 'Y': return "10001010100010000100" "00100";
    case 'F': return "11111100001111010000" "10000";
    case 'E': return "11111100001111010000" "11111";
    case 'B': return "11110100011111010001" "11110";
    case 'O': return "01110100011000110001" "01110";
    }
    return "00000000000000000000" "00000";
}
static void big_text(int x, int y, int sc, const char *s, u32 c) {
    for (; *s; s++) {
        if (*s != ' ') {
            const char *g = glyph(*s);
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < 5; k++)
                    if (g[r * 5 + k] == '1') rect(x + k * sc, y + r * sc, sc, sc, c);
        }
        x += (*s == ' ') ? sc * 3 : sc * 6;
    }
}
static int big_width(int sc, const char *s) {
    int w = 0;
    for (; *s; s++) w += (*s == ' ') ? sc * 3 : sc * 6;
    return w - sc;
}
static void draw_title(void) {
    rect(0, 0, W, H, PINK);
    for (int x = 0; x < W; x += 28) rect(x, 0, 12, H, RGB(255, 218, 222));
    const char *t = "MY FEMBOY";
    int sc = 7;
    int x = (W - big_width(sc, t)) / 2;
    big_text(x + 3, 53, sc, t, OUTL);
    big_text(x, 50, sc, t, CORAL);
    heart(W / 2, 112, 10, RGB(250, 100, 140));
    for (int i = 0; i < 16; i++)
        if (hearts[i].life > 0) {
            hearts[i].y += hearts[i].vy;
            hearts[i].life--;
            heart((int)hearts[i].x, (int)hearts[i].y, 5, RGB(250, 100, 140));
        }
    if (frame % 20 == 0) spawn_heart(W / 2, 130);
    orect(W / 2 - 85, 168, 170, 22, WHITE);
    if ((frame / 30) % 2 == 0) text(W / 2 - 70, 175, OUTL, "Press X to start");
    text(W / 2 - 91, 240, RGB(120, 60, 80), "Pet, feed, dress up your kitty!");
}

// ---------- игровая логика ----------
static void do_use(void) {
    sleeping = (menu == M_SLEEP) ? !sleeping : 0;
    switch (menu) {
    case M_FEED:
        if (cookies >= food_cost[food]) {
            cookies -= food_cost[food];
            hunger = clampf(hunger + food_gain[food]);
            if (food == 3 && want() == 0) love = clampf(love + 10);
            eat_t = 70;
            mood_t = 100;
            say("Yum!");
        } else say("Not enough cookies!");
        break;
    case M_DRINK:
        if (cookies >= 2) {
            cookies -= 2;
            thirst = clampf(thirst + 30);
            drink_t = 60;
            mood_t = 80;
            say("Gulp gulp~");
        } else say("Not enough cookies!");
        break;
    case M_SLEEP:
        say(sleeping ? "Zzz..." : "Good morning!");
        break;
    default: break;
    }
}

static void next_outfit(void) {
    int n = (outfit + 1) % NOUT;
    if (!owned[n]) {
        if (cookies >= outfit_price[n]) {
            cookies -= outfit_price[n];
            owned[n] = 1;
            say("Bought new outfit!");
        } else {
            char b[48];
            snprintf(b, sizeof(b), "%s costs %d cookies", outfit_names[n], outfit_price[n]);
            say(b);
            return;
        }
    }
    outfit = n;
    mood_t = 80;
}

int main(void) {
    setup_callbacks();
    pspDebugScreenInit();
    pspDebugScreenEnableBackColor(0);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    int cur = 0;
    SceCtrlData pad, old;
    memset(&old, 0, sizeof(old));
    memset(hearts, 0, sizeof(hearts));
    srand(sceKernelGetSystemTimeLow());

    while (running) {
        sceCtrlPeekBufferPositive(&pad, 1);
        u32 pressed = pad.Buttons & ~old.Buttons;
        old = pad;
        frame++;
        if (title) {
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_START)) title = 0;
            pressed = 0;
        }

        // ввод
        if (pressed & PSP_CTRL_RIGHT) { menu = (menu + 1) % M_COUNT; sleeping = 0; }
        if (pressed & PSP_CTRL_LEFT) { menu = (menu + M_COUNT - 1) % M_COUNT; sleeping = 0; }
        if (menu == M_FEED) {
            if (pressed & PSP_CTRL_UP) food = (food + 1) % NFOOD;
            if (pressed & PSP_CTRL_DOWN) food = (food + NFOOD - 1) % NFOOD;
        }
        if (pressed & PSP_CTRL_CROSS) do_use();
        if (pressed & PSP_CTRL_SQUARE) next_outfit();
        if (pressed & PSP_CTRL_TRIANGLE) room = !room;

        // поглаживание стиком
        int sx = (int)pad.Lx - 128;
        int dx = 0;
        if (menu == M_PET && !sleeping) {
            dx = sx / 16;
            if (abs(sx) > 50) {
                int dir = sx > 0 ? 1 : -1;
                if (dir != lastdir) {
                    lastdir = dir;
                    strokes++;
                    love = clampf(love + 3);
                    mood_t = 90;
                    spawn_heart(W / 2, 40);
                    if (strokes % 4 == 0) cookies++;
                    if (strokes % 25 == 0) { gems++; say("+1 gem!"); }
                }
            }
        } else lastdir = 0;

        // потребности
        hunger = clampf(hunger - 0.006f);
        thirst = clampf(thirst - 0.007f);
        love = clampf(love - 0.005f);
        if (sleeping) energy = clampf(energy + 0.12f);
        else energy = clampf(energy - 0.004f);
        if (sleeping && energy >= 100) { sleeping = 0; say("Fully rested!"); }

        if (eat_t > 0) eat_t--;
        if (drink_t > 0) drink_t--;
        if (mood_t > 0) mood_t--;
        if (msg_t > 0) msg_t--;

        int mood = 0;
        float avg = (hunger + thirst + energy + love) / 4;
        if (sleeping) mood = 3;
        else if (mood_t > 0 || avg > 75) mood = 1;
        else if (want() >= 0) mood = 2;

        // отрисовка в задний буфер
        u32 off = cur * FBSZ;
        fb = (u32 *)((u8 *)sceGeEdramGetAddr() + off + 0x40000000);
        pspDebugScreenSetOffset(off);

        if (title) {
            draw_title();
            sceDisplayWaitVblankStart();
            sceDisplaySetFrameBuf((void *)(0x04000000 + off), BUFW, PSP_DISPLAY_PIXEL_FORMAT_8888,
                                  PSP_DISPLAY_SETBUF_NEXTFRAME);
            cur ^= 1;
            continue;
        }

        draw_room_back();
        draw_cat(W / 2, dx, mood);
        draw_room_front();

        // еда на столе
        if (eat_t > 0) icon(food == 3 ? 3 : food, W / 2 + 40, 196, 14);
        if (drink_t > 0) icon(4, W / 2 + 40, 196, 14);

        // сердечки
        for (int i = 0; i < 16; i++)
            if (hearts[i].life > 0) {
                hearts[i].y += hearts[i].vy;
                hearts[i].life--;
                heart((int)hearts[i].x, (int)hearts[i].y, 5, RGB(250, 100, 140));
            }

        if (sleeping) {
            // затемнение + Zzz
            for (int y = 0; y < H; y += 2) rect(0, y, W, 1, RGB(60, 50, 90));
            text(W / 2 + 40, 60 - (frame / 4) % 20, RGB(255, 255, 255), "Z");
            text(W / 2 + 56, 44 - (frame / 5) % 20, RGB(255, 255, 255), "z");
        }

        draw_ui(want());

        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf((void *)(0x04000000 + off), BUFW, PSP_DISPLAY_PIXEL_FORMAT_8888,
                              PSP_DISPLAY_SETBUF_NEXTFRAME);
        cur ^= 1;
    }

    sceKernelExitGame();
    return 0;
}
