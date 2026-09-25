#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_TRect
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TSubMenu
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace
{

struct TrackerItem
{
    const char *text;
    bool done;
};

class TTrackerView : public TView
{
public:
    explicit TTrackerView(const TRect &bounds) : TView(bounds)
    {
        eventMask |= evKeyboard;
        options |= ofSelectable;
        setCursor(2, 2);
    }

    void draw() override
    {
        TDrawBuffer buf;
        auto normal = getColor(1);
        auto active = getColor(2);

        for (int y = 0; y < size.y; ++y)
        {
            buf.moveChar(0, ' ', normal, size.x);

            if (y == 0)
                buf.moveStr(0, "Turbo Tracker", normal);
            else if (y == 1)
                buf.moveStr(0, "Space toggles, j/k move, q quits", normal);
            else
            {
                int index = y - 3;
                if (0 <= index && index < static_cast<int>(items.size()))
                {
                    auto color = index == selected ? active : normal;
                    char line[128];
                    std::snprintf(line, sizeof(line), "%c [%c] %s",
                                  index == selected ? '>' : ' ',
                                  items[index].done ? 'x' : ' ',
                                  items[index].text);
                    buf.moveStr(0, line, color);
                }
            }

            writeLine(0, y, size.x, 1, buf);
        }

        setCursor(2, selected + 3);
        showCursor();
    }

    void handleEvent(TEvent &event) override
    {
        TView::handleEvent(event);

        if (event.what != evKeyDown)
            return;

        switch (event.keyDown.keyCode)
        {
        case kbUp:
            moveSelection(-1);
            clearEvent(event);
            break;
        case kbDown:
            moveSelection(1);
            clearEvent(event);
            break;
        default:
            switch (event.keyDown.charScan.charCode)
            {
            case 'k':
            case 'K':
                moveSelection(-1);
                clearEvent(event);
                break;
            case 'j':
            case 'J':
                moveSelection(1);
                clearEvent(event);
                break;
            case ' ':
                items[selected].done = !items[selected].done;
                drawView();
                clearEvent(event);
                break;
            case 'q':
            case 'Q':
                event.what = evCommand;
                event.message.command = cmQuit;
                putEvent(event);
                clearEvent(event);
                break;
            default:
                break;
            }
        }
    }

    TPalette &getPalette() const override
    {
        static TPalette palette("\x06\x1f", 2);
        return palette;
    }

private:
    void moveSelection(int delta)
    {
        selected = std::max(0, std::min<int>(selected + delta, items.size() - 1));
        drawView();
    }

    std::array<TrackerItem, 4> items {{
        {"Build native Turbo Vision app", true},
        {"Keep the port tiny and readable", true},
        {"Try the key handling", false},
        {"Decide whether web support is next", false},
    }};
    int selected = 0;
};

class TTrackerWindow : public TWindow
{
public:
    TTrackerWindow() :
        TWindowInit(&TTrackerWindow::initFrame),
        TWindow(TRect(5, 2, 63, 15), "Tracker", wnNoNumber)
    {
        TRect bounds = getExtent();
        bounds.grow(-2, -2);
        insert(new TTrackerView(bounds));
    }
};

class TTrackerApp : public TApplication
{
public:
    TTrackerApp() :
        TProgInit(&TTrackerApp::initStatusLine,
                  &TTrackerApp::initMenuBar,
                  &TTrackerApp::initDeskTop)
    {
        deskTop->insert(new TTrackerWindow());
    }

    static TMenuBar *initMenuBar(TRect r)
    {
        r.b.y = r.a.y + 1;
        return new TMenuBar(r,
            *new TSubMenu("~T~racker", kbAltT) +
                *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X"));
    }

    static TStatusLine *initStatusLine(TRect r)
    {
        r.a.y = r.b.y - 1;
        return new TStatusLine(r,
            *new TStatusDef(0, 0xFFFF) +
                *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit));
    }
};

} // namespace

int main()
{
    TTrackerApp app;
    app.run();
    return 0;
}
