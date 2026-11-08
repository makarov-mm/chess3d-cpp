#include "App.h"

#include "BoardGeometry.h"
#include "PieceModels.h"
#include "Themes.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <random>
#include <stdexcept>
#include <utility>

using namespace math;
using chess::Color;
using chess::PieceType;
using ui::FontId;
using ui::Rect;
using ui::Rgba;

namespace {

constexpr wchar_t ClassName[] = L"Chess3D.Window";
constexpr float PanelWidth = 344, PanelMargin = 16;

enum Widget {
    HelpButton = 1,
    UndoButton,
    RedoButton,
    HintButton,
    NewGameButton,
    FlipButton,
    ModeSelector,
    LevelSelector = ModeSelector + 2,
    ThemeSelector = LevelSelector + 2,
    SoundButton = ThemeSelector + 2,
    LanguageButton,
    ModalWidgets = 100, // the widgets of the help and of the promotion choice
    HelpClose,
    PromotionTile,
};

const ui::Color TextColor = Rgba(236, 239, 245);
const ui::Color MutedColor = Rgba(150, 158, 176);

chess::SearchLimits LevelLimits(int level, std::uint64_t seed)
{
    switch (level) {
    case 1: return {1, 3000, 180, seed};
    case 2: return {2, 3000, 70, seed};
    case 3: return {4, 2500, 22, seed};
    case 4: return {64, 1200, 0, seed};
    default: return {64, 3500, 0, seed};
    }
}

int MaterialValue(PieceType type)
{
    switch (type) {
    case PieceType::Pawn: return 1;
    case PieceType::Knight:
    case PieceType::Bishop: return 3;
    case PieceType::Rook: return 5;
    case PieceType::Queen: return 9;
    default: return 0;
    }
}

} // namespace

// Setup --------------------------------------------------------------------------------------------

App::App(HINSTANCE instance) : m_instance(instance)
{
    m_settings = Settings::Load();
    m_language = m_settings.language;
    m_theme = m_settings.theme;
    m_level = m_settings.level;
    m_mode = m_settings.mode;
    m_sound.enabled = m_settings.sound;
    m_panelVisible = m_settings.panel;
    m_panelSlide = m_panelVisible ? 1.0f : 0.0f;

    CreateMainWindow();
    m_context = std::make_unique<gl::Context>(m_window);
    render::Program::SetVersion(m_context->Version());
    m_painter = std::make_unique<ui::Painter>();
    BuildFonts();
    ShowWindow(m_window, m_settings.maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(m_window);
    ShowLoading();

#ifdef _DEBUG
    constexpr float detail = 0.6f; // unoptimised builds model the pieces more coarsely, to start quickly
#else
    constexpr float detail = 1.0f;
#endif
    const auto shapes = pieces::BuildPieceShapes(detail);
    m_renderer = std::make_unique<render::Renderer>(shapes, m_context->Version());
    m_renderer->SetTheme(Themes()[m_theme]);
    std::array<float, 6> heights{}, radii{};
    for (int i = 0; i < 6; ++i) {
        heights[i] = shapes[i].height;
        radii[i] = shapes[i].radius;
    }
    m_view = std::make_unique<BoardView>(heights, radii);

    if (auto saved = chess::Game::Deserialize(m_settings.game))
        m_game = std::move(*saved);
    m_announcedEnd = m_game.Status().Over();
    m_view->Rebuild(m_game, true);
    m_camera.Reset(m_mode == PlayBlack ? Color::Black : Color::White);
    m_lastFrame = std::chrono::steady_clock::now();
}

App::~App()
{
    m_computer.Cancel();
    m_hinter.Cancel();
}

void App::CreateMainWindow()
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = m_instance;
    windowClass.hIcon = LoadIconW(m_instance, MAKEINTRESOURCEW(1));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = ClassName;
    RegisterClassExW(&windowClass);

    RECT rect{};
    if (m_settings.hasWindow && MonitorFromRect(&m_settings.window, MONITOR_DEFAULTTONULL)) {
        rect = m_settings.window;
    }
    else {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int dpi = static_cast<int>(GetDpiForSystem());
        const int workWidth = work.right - work.left, workHeight = work.bottom - work.top;
        const int width = std::min(MulDiv(1500, dpi, 96), workWidth * 92 / 100);
        const int height = std::min(MulDiv(920, dpi, 96), workHeight * 92 / 100);
        rect = {work.left + (workWidth - width) / 2, work.top + (workHeight - height) / 2, 0, 0};
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
    }

    m_window = CreateWindowExW(0, ClassName, L"3D Chess", WS_OVERLAPPEDWINDOW, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                               nullptr, m_instance, this);
    if (!m_window)
        throw std::runtime_error("The window could not be created.");
    m_dpi = GetDpiForWindow(m_window);
    m_placement.length = sizeof(m_placement);
    // A dark title bar, to go with the game, where Windows offers one.
    constexpr DWORD UseImmersiveDarkMode = 20; // DWMWA_USE_IMMERSIVE_DARK_MODE, Windows 10 20H1+
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(m_window, UseImmersiveDarkMode, &dark, sizeof(dark));
}

void App::BuildFonts()
{
    const float scale = Scale();
    m_fonts[0] = std::make_unique<text::Font>(L"Segoe UI", 15 * scale, FW_NORMAL);
    m_fonts[1] = std::make_unique<text::Font>(L"Segoe UI", 19 * scale, FW_SEMIBOLD);
    m_fonts[2] = std::make_unique<text::Font>(L"Segoe UI", 31 * scale, FW_SEMIBOLD);
    m_painter->SetFonts(m_fonts[0].get(), m_fonts[1].get(), m_fonts[2].get());
    m_fontDpi = m_dpi;
}

void App::ShowLoading()
{
    RECT client{};
    GetClientRect(m_window, &client);
    m_width = client.right;
    m_height = client.bottom;
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
    gl::glViewport(0, 0, m_width, m_height);
    gl::glClearColor(0.016f, 0.017f, 0.022f, 1);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);
    m_painter->Begin(m_width, m_height);
    const Rect screen{0, 0, static_cast<float>(m_width), static_cast<float>(m_height)};
    m_painter->TextCentered(FontId::Display, screen, T(Str::Title), TextColor);
    m_painter->Flush();
    m_context->SwapBuffers();
}

int App::Run()
{
    MSG message{};
    while (m_running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                m_running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!m_running)
            break;
        if (IsIconic(m_window)) {
            WaitMessage();
            m_lastFrame = std::chrono::steady_clock::now();
            continue;
        }

        // Nothing moving and nothing new: wait for input instead of drawing the same picture again.
        if (!m_dirty && !NeedsAnimation()) {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            m_lastFrame = std::chrono::steady_clock::now(); // the idle time is not animation time
            continue;
        }
        m_dirty = false;
        const auto start = std::chrono::steady_clock::now();
        Frame();
        // Swapping the buffers waits for the screen; where it does not, the loop sleeps a little.
        if (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2))
            Sleep(1);
    }
    return 0;
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        static_cast<App*>(create->lpCreateParams)->m_window = window;
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return app ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    m_dirty = true;
    auto pointerAt = [&] {
        m_pointer.x = static_cast<float>(static_cast<short>(LOWORD(lParam)));
        m_pointer.y = static_cast<float>(static_cast<short>(HIWORD(lParam)));
    };

    switch (message) {
    case WM_CLOSE:
        Save();
        DestroyWindow(m_window);
        return 0;
    case WM_DESTROY:
        m_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(m_window, &paint);
        EndPaint(m_window, &paint);
        // While the window is being resized the loop does not run; the frames come from here.
        if (m_renderer && m_width > 0 && m_height > 0)
            Frame();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DPICHANGED: {
        m_dpi = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {MulDiv(900, static_cast<int>(m_dpi), 96), MulDiv(600, static_cast<int>(m_dpi), 96)};
        return 0;
    }
    case WM_MOUSEMOVE:
        pointerAt();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        pointerAt();
        SetCapture(m_window);
        m_pointer.leftDown = m_pointer.leftPressed = true;
        return 0;
    case WM_LBUTTONUP:
        pointerAt();
        m_pointer.leftDown = false;
        m_pointer.leftReleased = true;
        if (!m_pointer.rightDown)
            ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        pointerAt();
        SetCapture(m_window);
        m_pointer.rightDown = m_pointer.rightPressed = true;
        return 0;
    case WM_RBUTTONUP:
        pointerAt();
        m_pointer.rightDown = false;
        m_pointer.rightReleased = true;
        if (!m_pointer.leftDown)
            ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (m_pointer.leftDown)
            m_pointer.leftReleased = true;
        if (m_pointer.rightDown)
            m_pointer.rightReleased = true;
        m_pointer.leftDown = m_pointer.rightDown = false;
        return 0;
    case WM_MOUSEWHEEL:
        m_pointer.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
        return 0;
    case WM_KEYDOWN:
        HandleKey(wParam);
        return 0;
    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN && (HIWORD(lParam) & KF_ALTDOWN)) {
            ToggleFullScreen();
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, m_cursorHand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(m_window, message, wParam, lParam);
}

// The frame ----------------------------------------------------------------------------------------

void App::Frame()
{
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::min(std::chrono::duration<float>(now - m_lastFrame).count(), 0.1f);
    m_lastFrame = now;
    m_time += seconds;
    if (m_width <= 0 || m_height <= 0)
        return;
    if (m_dpi != m_fontDpi)
        BuildFonts();
    m_renderer->Resize(m_width, m_height);

    Update(seconds);

    const Theme& theme = Themes()[m_theme];
    render::Scene scene;
    scene.view = m_camera.View();
    scene.projection = m_camera.Projection(static_cast<float>(m_width) / static_cast<float>(m_height));
    scene.eye = m_camera.Eye();
    scene.time = m_time;
    scene.pieces = m_view->Draws(m_selected, m_hoverPiece, theme.accent, m_time);
    scene.contacts = m_view->Contacts();

    auto bit = [](int square) { return square >= 0 ? std::uint64_t{1} << square : 0; };
    scene.marks.selected = bit(m_selected);
    for (const chess::Move& move : m_selectedMoves)
        (m_game.Current().IsCapture(move) ? scene.marks.capture : scene.marks.quiet) |= bit(move.to);
    if (!m_game.Moves().empty()) {
        const chess::Move last = m_game.Moves().back().move;
        scene.marks.lastMove = bit(last.from) | bit(last.to);
    }
    if (m_game.Status().check)
        scene.marks.check = bit(m_game.Current().KingSquare(m_game.Current().SideToMove()));
    scene.marks.hover = bit(m_hoverSquare);
    if (m_hint && m_hintGlow > 0.01f)
        scene.arrow = render::Arrow{board::SquareCenter(m_hint->from), board::SquareCenter(m_hint->to), theme.accent * 3.5f, 0.9f * m_hintGlow};
    scene.panels = FrostedPanels();
    m_renderer->Render(scene);

    m_cursorHand = false;
    m_painter->Begin(m_width, m_height);
    DrawInterface();
    m_painter->Flush();
    HandleBoardPointer();

    m_pointer.leftPressed = m_pointer.leftReleased = m_pointer.rightPressed = m_pointer.rightReleased = false;
    m_pointer.wheel = 0;
    m_pointer.consumed = false;
    m_context->SwapBuffers();
}

void App::Update(float seconds)
{
    const float target = m_panelVisible ? 1.0f : 0.0f;
    m_panelSlide += (target - m_panelSlide) * (1 - std::exp(-seconds * 12));
    if (std::abs(target - m_panelSlide) < 0.001f)
        m_panelSlide = target;
    const float panelSpace = (PanelWidth + PanelMargin) * Scale() * m_panelSlide;
    m_camera.SetSpace(1 - panelSpace / static_cast<float>(std::max(m_width, 1)));
    m_camera.Update(seconds);
    m_view->Update(seconds);

    const bool over = m_game.Status().Over();
    m_bannerFade = std::clamp(m_bannerFade + (over ? seconds : -seconds) * 3, 0.0f, 1.0f);
    m_hintGlow = std::clamp(m_hintGlow + (m_hint ? seconds : -seconds) * 4, 0.0f, 1.0f);
    for (Toast& toast : m_toasts)
        toast.age += seconds;
    std::erase_if(m_toasts, [](const Toast& toast) { return toast.age > 2.8f; });

    if (m_hintThinking) {
        if (auto result = m_hinter.TakeResult()) {
            m_hintThinking = false;
            if (!result->best.IsNull() && HumanToMove()) {
                m_hint = result->best;
                ShowToast(std::string(T(Str::HintToast)) + m_game.Current().ToSan(result->best));
            }
        }
        else if (!m_hinter.Busy()) {
            m_hintThinking = false;
        }
    }
    UpdateComputer(seconds);

    if (over && !m_announcedEnd) {
        m_announcedEnd = true;
        m_sound.Play(SoundBank::Effect::GameOver);
    }
    if (!over)
        m_announcedEnd = false;
}

bool App::NeedsAnimation() const
{
    return m_camera.Moving() || m_view->Animating() || m_computerThinking || m_computerResult || m_hintThinking || m_selected >= 0 ||
           m_game.Status().check || !m_toasts.empty() || (m_panelSlide > 0 && m_panelSlide < 1) || (m_bannerFade > 0 && m_bannerFade < 1) ||
           m_hintGlow > 0 || m_promotion || m_gesture != Gesture::None || m_pointer.leftDown || m_pointer.rightDown ||
           (!m_game.Status().Over() && !IsHuman(m_game.Current().SideToMove()));
}

// The game -----------------------------------------------------------------------------------------

bool App::IsHuman(Color side) const
{
    switch (m_mode) {
    case PlayWhite: return side == Color::White;
    case PlayBlack: return side == Color::Black;
    case TwoPlayers: return true;
    default: return false;
    }
}

bool App::HumanToMove() const
{
    return !m_game.Status().Over() && IsHuman(m_game.Current().SideToMove());
}

void App::StopComputer()
{
    m_computer.Cancel();
    m_computerThinking = false;
    m_computerResult.reset();
    m_hinter.Cancel();
    m_hintThinking = false;
}

void App::UpdateComputer(float seconds)
{
    if (m_promotion || m_game.Status().Over() || IsHuman(m_game.Current().SideToMove()))
        return;
    m_computerClock += seconds;
    if (!m_computerThinking && !m_computerResult) {
        static std::mt19937_64 random{std::random_device{}()};
        m_computer.Start(m_game.Current(), m_game.RepetitionHistory(), LevelLimits(m_level, random()));
        m_computerThinking = true;
        m_computerClock = 0;
    }
    if (m_computerThinking) {
        if (auto result = m_computer.TakeResult()) {
            m_computerResult = result;
            m_computerThinking = false;
        }
        else if (!m_computer.Busy()) {
            m_computerThinking = false; // cancelled; a new search starts next frame
        }
    }
    const float pause = m_mode == Demo ? 0.9f : 0.45f;
    if (m_computerResult && !m_view->Animating() && m_computerClock >= pause && m_gesture == Gesture::None) {
        const chess::Move move = m_computerResult->best;
        m_computerResult.reset();
        if (!move.IsNull())
            PlayMove(move);
    }
}

void App::NewGame()
{
    StopComputer();
    m_computer.NewGame();
    m_game.Reset(chess::Position::Start());
    m_view->Rebuild(m_game, true);
    m_camera.Reset(m_mode == PlayBlack ? Color::Black : Color::White);
    m_announcedEnd = false;
    AfterPositionChange();
}

void App::PlayMove(chess::Move move)
{
    if (!m_game.Play(move))
        return;
    m_view->PlayMove(m_game.Moves().back(), true);
    Announce(m_game.Moves().back());
    AfterPositionChange();
}

void App::Announce(const chess::MoveRecord& record)
{
    if (m_game.Status().Over())
        return; // the end has its own sound
    if (m_game.Status().check)
        m_sound.Play(SoundBank::Effect::Check);
    else if (record.move.IsCastling())
        m_sound.Play(SoundBank::Effect::Castle);
    else if (!record.captured.Empty())
        m_sound.Play(SoundBank::Effect::Capture);
    else
        m_sound.Play(SoundBank::Effect::Move);
}

void App::Undo()
{
    StopComputer();
    if (m_promotion) {
        m_promotion.reset();
        return;
    }
    if (!m_game.CanUndo()) {
        ShowToast(std::string(T(Str::NothingToUndo)));
        return;
    }
    auto undoOne = [&] {
        const chess::MoveRecord record = m_game.Moves().back();
        m_game.Undo();
        m_view->PlayMove(record, false);
    };
    undoOne();
    // Against the computer, back to the player's own turn.
    if ((m_mode == PlayWhite || m_mode == PlayBlack) && !IsHuman(m_game.Current().SideToMove()) && m_game.CanUndo())
        undoOne();
    m_sound.Play(SoundBank::Effect::Move);
    AfterPositionChange();
}

void App::Redo()
{
    StopComputer();
    if (!m_game.CanRedo())
        return;
    m_game.Redo();
    m_view->PlayMove(m_game.Moves().back(), true);
    if ((m_mode == PlayWhite || m_mode == PlayBlack) && !IsHuman(m_game.Current().SideToMove()) && m_game.CanRedo()) {
        m_game.Redo();
        m_view->PlayMove(m_game.Moves().back(), true);
    }
    Announce(m_game.Moves().back());
    AfterPositionChange();
}

void App::SetMode(int mode)
{
    StopComputer();
    m_mode = (mode % 4 + 4) % 4;
    if (m_mode == PlayWhite || m_mode == PlayBlack)
        m_camera.FaceSide(m_mode == PlayBlack ? Color::Black : Color::White);
    ClearSelection();
    Save();
}

void App::SetLevel(int level)
{
    m_level = std::clamp(level, 1, 5);
    if (m_computerThinking)
        StopComputer(); // starts again at the new level
    Save();
}

void App::SetTheme(int theme)
{
    m_theme = (theme % 3 + 3) % 3;
    m_renderer->SetTheme(Themes()[m_theme]);
    Save();
}

void App::RequestHint()
{
    if (!HumanToMove() || m_hintThinking || m_promotion)
        return;
    m_hint.reset();
    m_hinter.Start(m_game.Current(), m_game.RepetitionHistory(), {64, 1200, 0, 0});
    m_hintThinking = true;
}

void App::AfterPositionChange()
{
    ClearSelection();
    m_hint.reset();
    m_promotion.reset();
    m_followMoves = true;
    Save();
}

void App::ClearSelection()
{
    m_selected = -1;
    m_selectedMoves.clear();
    m_hoverSquare = -1;
}

void App::Select(int square)
{
    m_selected = square;
    m_selectedMoves = m_game.Current().LegalMovesFrom(square);
}

void App::TryMove(int from, int to)
{
    std::vector<chess::Move> choices;
    for (const chess::Move& move : m_game.Current().LegalMovesFrom(from))
        if (move.to == to)
            choices.push_back(move);
    if (choices.empty())
        return;
    if (choices.size() > 1) {
        m_promotion = choices; // the player picks the piece
        m_view->Release(from);
        return;
    }
    PlayMove(choices.front());
}

void App::CopyGame(bool fen)
{
    const std::wstring text = win32::Widen(fen ? m_game.Current().ToFen() : m_game.ToPgn());
    if (!OpenClipboard(m_window))
        return;
    EmptyClipboard();
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t))) {
        if (void* target = GlobalLock(memory)) {
            std::memcpy(target, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        }
        else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    ShowToast(std::string(T(fen ? Str::FenCopied : Str::PgnCopied)));
}

void App::PasteGame()
{
    std::string text;
    if (OpenClipboard(m_window)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* characters = static_cast<const wchar_t*>(GlobalLock(data))) {
                text = win32::Narrow(characters);
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    auto game = chess::Game::Parse(text);
    if (!game) {
        ShowToast(std::string(T(Str::NothingToPaste)));
        return;
    }
    StopComputer();
    m_game = std::move(*game);
    m_view->Rebuild(m_game, true);
    m_announcedEnd = m_game.Status().Over();
    AfterPositionChange();
    ShowToast(std::string(T(Str::GameLoaded)));
}

void App::Save()
{
    m_settings.theme = m_theme;
    m_settings.level = m_level;
    m_settings.mode = m_mode;
    m_settings.sound = m_sound.enabled;
    m_settings.panel = m_panelVisible;
    m_settings.language = m_language;
    m_settings.game = m_game.Serialize();
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (m_fullScreen)
        placement = m_placement;
    else if (m_window)
        GetWindowPlacement(m_window, &placement);
    if (placement.rcNormalPosition.right > placement.rcNormalPosition.left) {
        m_settings.hasWindow = true;
        m_settings.window = placement.rcNormalPosition;
        m_settings.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    }
    m_settings.Save();
}

void App::ToggleFullScreen()
{
    const LONG style = GetWindowLongW(m_window, GWL_STYLE);
    if (!m_fullScreen) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        m_placement.length = sizeof(m_placement);
        if (GetWindowPlacement(m_window, &m_placement) && GetMonitorInfoW(MonitorFromWindow(m_window, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
            SetWindowLongW(m_window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            const RECT& r = monitor.rcMonitor;
            SetWindowPos(m_window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullScreen = true;
        }
    }
    else {
        SetWindowLongW(m_window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_window, &m_placement);
        SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_fullScreen = false;
    }
}

// Input --------------------------------------------------------------------------------------------

Vec3 App::PointerOnPlane(float height) const
{
    Vec3 origin, direction;
    m_camera.Ray(m_pointer.x, m_pointer.y, static_cast<float>(m_width), static_cast<float>(m_height), origin, direction);
    if (std::abs(direction.y) < 1e-5f)
        return origin;
    const float t = (height - origin.y) / direction.y;
    return origin + direction * std::max(t, 0.0f);
}

int App::SquareUnderPointer() const
{
    return board::SquareAt(PointerOnPlane(0));
}

void App::HandleBoardPointer()
{
    const bool blocked = m_pointer.consumed || m_helpVisible || m_promotion.has_value();
    const float scale = Scale();
    auto isTarget = [&](int square) {
        return square >= 0 && std::ranges::any_of(m_selectedMoves, [square](const chess::Move& move) { return move.to == square; });
    };
    auto pointedSquare = [&] {
        Vec3 origin, direction;
        m_camera.Ray(m_pointer.x, m_pointer.y, static_cast<float>(m_width), static_cast<float>(m_height), origin, direction);
        const int piece = m_view->Pick(origin, direction);
        return piece >= 0 ? piece : SquareUnderPointer();
    };
    auto movable = [&](int square) {
        if (square < 0 || !HumanToMove())
            return false;
        const chess::Piece piece = m_game.Current().At(square);
        return !piece.Empty() && piece.color == m_game.Current().SideToMove();
    };

    // What the pointer is over.
    const bool insideWindow = m_pointer.x >= 0 && m_pointer.y >= 0 && m_pointer.x < static_cast<float>(m_width) && m_pointer.y < static_cast<float>(m_height);
    if (m_gesture == Gesture::None || m_gesture == Gesture::DragPiece) {
        const int square = insideWindow && !blocked ? pointedSquare() : -1;
        m_hoverPiece = m_gesture == Gesture::None && movable(square) ? square : -1;
        m_hoverSquare = isTarget(m_gesture == Gesture::DragPiece ? SquareUnderPointer() : square) ? (m_gesture == Gesture::DragPiece ? SquareUnderPointer() : square) : -1;
    }
    else {
        m_hoverPiece = m_hoverSquare = -1;
    }
    if (m_hoverPiece >= 0 || m_hoverSquare >= 0 || m_gesture == Gesture::DragPiece)
        m_cursorHand = true;

    if (!blocked && m_pointer.leftPressed) {
        const int square = pointedSquare();
        m_pressX = m_dragX = m_pointer.x;
        m_pressY = m_dragY = m_pointer.y;
        if (m_selected >= 0 && isTarget(square)) {
            TryMove(m_selected, square);
        }
        else if (movable(square)) {
            if (square != m_selected)
                Select(square);
            m_gesture = Gesture::PressPiece;
            m_pressSquare = square;
        }
        else {
            m_gesture = Gesture::TurnCamera;
            m_pressSquare = -1;
        }
    }
    if (!m_pointer.consumed && m_pointer.rightPressed && m_gesture == Gesture::None) {
        m_gesture = Gesture::TurnCamera;
        m_pressSquare = -1;
        m_pressX = m_dragX = m_pointer.x;
        m_pressY = m_dragY = m_pointer.y;
    }

    const float travel = std::abs(m_pointer.x - m_pressX) + std::abs(m_pointer.y - m_pressY);
    if (m_gesture == Gesture::PressPiece && travel > 6 * scale && m_pointer.leftDown)
        m_gesture = Gesture::DragPiece;
    if (m_gesture == Gesture::DragPiece)
        m_view->Hold(m_pressSquare, PointerOnPlane(0.4f) + Vec3{0, 0.05f, 0});
    if (m_gesture == Gesture::TurnCamera) {
        m_camera.Rotate(-(m_pointer.x - m_dragX) * 0.0055f / scale, (m_pointer.y - m_dragY) * 0.0045f / scale);
        m_dragX = m_pointer.x;
        m_dragY = m_pointer.y;
    }

    if (m_pointer.leftReleased || m_pointer.rightReleased) {
        if (m_gesture == Gesture::DragPiece && m_pointer.leftReleased) {
            const int square = SquareUnderPointer();
            if (isTarget(square))
                TryMove(m_pressSquare, square);
            else
                m_view->Release(m_pressSquare);
        }
        else if (m_gesture == Gesture::TurnCamera && travel < 4 * scale && m_pointer.leftReleased) {
            ClearSelection(); // a click beside the pieces
        }
        if ((m_pointer.leftReleased && m_gesture != Gesture::None && !(m_gesture == Gesture::TurnCamera && m_pointer.rightDown)) ||
            (m_pointer.rightReleased && m_gesture == Gesture::TurnCamera && !m_pointer.leftDown))
            m_gesture = Gesture::None;
    }

    if (!m_pointer.consumed && m_pointer.wheel != 0)
        m_camera.Zoom(m_pointer.wheel);
}

void App::HandleKey(WPARAM key)
{
    const bool control = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;

    if (m_promotion) {
        PieceType type = PieceType::None;
        switch (key) {
        case 'Q': type = PieceType::Queen; break;
        case 'R': type = PieceType::Rook; break;
        case 'B': type = PieceType::Bishop; break;
        case 'N': type = PieceType::Knight; break;
        case VK_ESCAPE: m_promotion.reset(); return;
        default: return;
        }
        for (const chess::Move& move : *m_promotion)
            if (move.promotion == type) {
                PlayMove(move);
                return;
            }
        return;
    }
    if (m_helpVisible && (key == VK_ESCAPE || key == VK_F1)) {
        m_helpVisible = false;
        return;
    }

    if (control) {
        switch (key) {
        case 'Z': shift ? Redo() : Undo(); return;
        case 'Y': Redo(); return;
        case 'C': CopyGame(shift); return;
        case 'V': PasteGame(); return;
        case 'N': NewGame(); return;
        default: return;
        }
    }

    switch (key) {
    case VK_BACK:
    case VK_LEFT: Undo(); break;
    case VK_RIGHT: Redo(); break;
    case 'N': NewGame(); break;
    case 'H': RequestHint(); break;
    case 'F': m_camera.Flip(); break;
    case 'V': m_camera.ToggleTopView(); break;
    case VK_HOME: m_camera.Reset(m_mode == PlayBlack ? Color::Black : Color::White); break;
    case 'M': SetMode(m_mode + 1); break;
    case 'T': SetTheme(m_theme + (shift ? -1 : 1)); break;
    case 'S':
        m_sound.enabled = !m_sound.enabled;
        Save();
        break;
    case 'L':
        m_language = m_language == Language::English ? Language::Russian : Language::English;
        Save();
        break;
    case VK_TAB:
        m_panelVisible = !m_panelVisible;
        Save();
        break;
    case VK_F1: m_helpVisible = !m_helpVisible; break;
    case VK_F11: ToggleFullScreen(); break;
    case VK_ESCAPE:
        if (m_fullScreen)
            ToggleFullScreen();
        else
            ClearSelection();
        break;
    default:
        if (key >= '1' && key <= '5')
            SetLevel(static_cast<int>(key - '0'));
        else if (key >= VK_NUMPAD1 && key <= VK_NUMPAD5)
            SetLevel(static_cast<int>(key - VK_NUMPAD0));
        break;
    }
}

// The interface ------------------------------------------------------------------------------------

Rect App::PanelRect() const
{
    const float scale = Scale();
    const float width = PanelWidth * scale, margin = PanelMargin * scale;
    return {static_cast<float>(m_width) - (width + margin) * m_panelSlide, margin, width, static_cast<float>(m_height) - 2 * margin};
}

Rect App::FreeArea() const
{
    const Rect panel = PanelRect();
    return {0, 0, std::min(panel.x, static_cast<float>(m_width)), static_cast<float>(m_height)};
}

Rect App::HelpCard() const
{
    const float scale = Scale();
    const float width = 660 * scale, height = 20 * 27 * scale + 2 * 28 * scale + 52 * scale;
    const Rect free = FreeArea();
    return {free.x + (free.width - width) / 2, std::max(10 * scale, (static_cast<float>(m_height) - height) / 2), width, height};
}

Rect App::PromotionCard() const
{
    const float scale = Scale();
    const float width = 4 * 116 * scale + 3 * 14 * scale + 2 * 22 * scale, height = 116 * scale + 2 * 22 * scale + 44 * scale;
    const Rect free = FreeArea();
    return {free.x + (free.width - width) / 2, (static_cast<float>(m_height) - height) / 2, width, height};
}

Rect App::BannerCard() const
{
    const float scale = Scale();
    const chess::GameStatus status = m_game.Status();
    const std::string_view title = status.reason == chess::EndReason::Checkmate ? T(Str::Checkmate)
                                   : status.reason == chess::EndReason::Stalemate ? T(Str::Stalemate)
                                                                                  : T(Str::Draw);
    const float width = std::max(m_painter->Measure(FontId::Display, title) + 80 * scale, 300 * scale);
    const Rect free = FreeArea();
    return {free.x + (free.width - width) / 2, 26 * scale - (1 - m_bannerFade) * 20 * scale, width, 92 * scale};
}

std::vector<render::FrostedPanel> App::FrostedPanels() const
{
    std::vector<render::FrostedPanel> panels;
    const float scale = Scale();
    auto add = [&](Rect rect, float radius, float opacity) { panels.push_back({rect.x, rect.y, rect.width, rect.height, radius * scale, opacity}); };
    if (m_panelSlide > 0.001f)
        add(PanelRect(), 18, 1);
    if (m_bannerFade > 0.001f)
        add(BannerCard(), 18, m_bannerFade);
    if (m_promotion)
        add(PromotionCard(), 20, 1);
    if (m_helpVisible)
        add(HelpCard(), 20, 1);
    return panels;
}

void App::ShowToast(std::string text)
{
    if (m_toasts.size() >= 3)
        m_toasts.erase(m_toasts.begin());
    m_toasts.push_back({std::move(text), 0});
}

std::string App::PlayerName(Color side) const
{
    if (m_mode == TwoPlayers)
        return std::string(T(Str::Player));
    if (IsHuman(side))
        return std::string(T(Str::You));
    return std::format("{} · {}", T(Str::Computer), T(static_cast<Str>(std::to_underlying(Str::Level1) + m_level - 1)));
}

bool App::Button(int id, Rect rect, std::string_view label, bool enabled, bool accent)
{
    const float scale = Scale();
    const bool modal = m_helpVisible || m_promotion.has_value();
    const bool interactive = enabled && (!modal || id > ModalWidgets);
    const bool hover = interactive && rect.Contains(m_pointer.x, m_pointer.y);
    if (hover)
        m_cursorHand = true;
    if (hover && m_pointer.leftPressed) {
        m_activeWidget = id;
        m_pointer.consumed = true;
    }
    bool clicked = false;
    if (m_pointer.leftReleased && m_activeWidget == id) {
        clicked = hover;
        m_activeWidget = 0;
        m_pointer.consumed = true;
    }
    const bool pressed = hover && m_activeWidget == id && m_pointer.leftDown;
    ui::Color fill = !enabled ? Rgba(255, 255, 255, 7) : pressed ? Rgba(255, 255, 255, 52) : hover ? Rgba(255, 255, 255, 34) : Rgba(255, 255, 255, 17);
    if (accent && enabled)
        fill = ui::Mix(fill, ui::FromLinear(Themes()[m_theme].accent, static_cast<float>(fill.a) / 255.0f * 2.2f), 0.6f);
    m_painter->Fill(rect, 9 * scale, fill);
    m_painter->TextCentered(FontId::Body, rect, label, enabled ? TextColor : ui::Fade(TextColor, 0.35f));
    if (clicked)
        m_sound.Play(SoundBank::Effect::Click);
    return clicked;
}

int App::Selector(int id, Rect rect, std::string_view label, std::string_view value)
{
    const float scale = Scale();
    m_painter->Text(FontId::Body, rect.x, rect.y + (rect.height - m_painter->LineHeight(FontId::Body)) / 2, label, MutedColor);
    const float arrow = rect.height;
    const Rect right{rect.Right() - arrow, rect.y, arrow, rect.height};
    const Rect valueRect{rect.x + rect.width * 0.36f, rect.y, rect.width * 0.64f - arrow - 4 * scale, rect.height};
    const Rect left{valueRect.x - arrow - 4 * scale, rect.y, arrow, rect.height};
    m_painter->Fill({left.Right() + 2 * scale, rect.y, right.x - left.Right() - 4 * scale, rect.height}, 9 * scale, Rgba(255, 255, 255, 9));
    m_painter->TextCentered(FontId::Body, {left.Right(), rect.y, right.x - left.Right(), rect.height}, value, TextColor);
    int change = 0;
    if (Button(id, left, ""))
        change = -1;
    if (Button(id + 1, right, ""))
        change = 1;
    const float size = 5 * scale;
    m_painter->Arrow(left.x + left.width / 2, left.y + left.height / 2, size * 0.8f, size, -1, TextColor);
    m_painter->Arrow(right.x + right.width / 2, right.y + right.height / 2, size * 0.8f, size, 1, TextColor);
    return change;
}

void App::DrawInterface()
{
    DrawPanel();
    DrawBanner();
    DrawToasts();
    if (m_promotion)
        DrawPromotion();
    if (m_helpVisible)
        DrawHelp();
}

void App::DrawStatus(Rect area)
{
    const float scale = Scale();
    ui::Painter& p = *m_painter;
    const chess::GameStatus status = m_game.Status();
    const Color side = m_game.Current().SideToMove();
    const ui::Color accent = ui::FromLinear(Themes()[m_theme].accent);
    p.Fill(area, 12 * scale, Rgba(255, 255, 255, 14));

    // A disc of the side to move.
    const float discX = area.x + 22 * scale, discY = area.y + area.height / 2;
    p.Disc(discX, discY, 10 * scale, Rgba(120, 125, 140));
    p.Disc(discX, discY, 9 * scale, side == Color::White ? Rgba(238, 232, 220) : Rgba(22, 22, 26));

    std::string title, detail;
    ui::Color titleColor = TextColor;
    if (status.Over()) {
        title = status.reason == chess::EndReason::Checkmate ? T(Str::Checkmate) : status.reason == chess::EndReason::Stalemate ? T(Str::Stalemate) : T(Str::Draw);
        if (status.result == chess::GameResult::WhiteWins)
            detail = T(Str::WhiteWins);
        else if (status.result == chess::GameResult::BlackWins)
            detail = T(Str::BlackWins);
        else if (status.reason == chess::EndReason::Repetition)
            detail = T(Str::ByRepetition);
        else if (status.reason == chess::EndReason::FiftyMoves)
            detail = T(Str::ByFiftyMoves);
        else if (status.reason == chess::EndReason::InsufficientMaterial)
            detail = T(Str::ByMaterial);
        titleColor = accent;
    }
    else {
        title = status.check ? T(Str::Check) : T(side == Color::White ? Str::WhiteToMove : Str::BlackToMove);
        if (status.check)
            titleColor = Rgba(255, 120, 90);
        detail = m_computerThinking || m_computerResult ? std::string(T(Str::Thinking))
                 : status.check                         ? std::string(T(side == Color::White ? Str::WhiteToMove : Str::BlackToMove))
                                                        : PlayerName(side);
    }
    const float textX = area.x + 44 * scale;
    const float lineHeight = p.LineHeight(FontId::Heading);
    p.Text(FontId::Heading, textX, area.y + area.height / 2 - lineHeight + 3 * scale, title, titleColor);
    p.Text(FontId::Body, textX, area.y + area.height / 2 + 2 * scale, detail, MutedColor);
    if (m_computerThinking || m_computerResult || m_hintThinking)
        p.Spinner(area.Right() - 22 * scale, area.y + area.height / 2, 8 * scale, 2.2f * scale, m_time * 5.0f, accent);
}

void App::DrawMoveList(Rect area)
{
    const float scale = Scale();
    ui::Painter& p = *m_painter;
    p.Fill(area, 12 * scale, Rgba(0, 0, 0, 40));
    const auto moves = m_game.Moves();
    if (moves.empty()) {
        p.TextCentered(FontId::Body, area, T(Str::NoMovesYet), MutedColor);
        return;
    }

    const float rowHeight = std::round(25 * scale);
    const Rect inner = area.Inset(8 * scale);
    const int visible = std::max(1, static_cast<int>(inner.height / rowHeight));
    const bool blackFirst = m_game.Start().SideToMove() == Color::Black;
    const int firstNumber = m_game.Start().FullmoveNumber();
    const int rows = (static_cast<int>(moves.size()) + (blackFirst ? 1 : 0) + 1) / 2;
    const float maxScroll = static_cast<float>(std::max(0, rows - visible));

    if (area.Contains(m_pointer.x, m_pointer.y) && m_pointer.wheel != 0 && !m_helpVisible) {
        m_moveScroll = std::clamp(m_moveScroll - m_pointer.wheel * 3, 0.0f, maxScroll);
        m_followMoves = m_moveScroll >= maxScroll;
        m_pointer.wheel = 0;
    }
    if (m_followMoves)
        m_moveScroll = maxScroll;
    m_moveScroll = std::clamp(m_moveScroll, 0.0f, maxScroll);

    const int first = static_cast<int>(m_moveScroll);
    const float numberWidth = 44 * scale, columnWidth = (inner.width - numberWidth) / 2;
    const ui::Color accent = ui::FromLinear(Themes()[m_theme].accent);
    for (int row = first; row < std::min(rows, first + visible); ++row) {
        const float y = inner.y + static_cast<float>(row - first) * rowHeight;
        const std::string number = std::format("{}.", firstNumber + row);
        p.Text(FontId::Body, inner.x + numberWidth - 10 * scale - p.Measure(FontId::Body, number), y + 3 * scale, number, MutedColor);
        for (int column = 0; column < 2; ++column) {
            const int ply = row * 2 + column - (blackFirst ? 1 : 0);
            if (ply < 0 || ply >= static_cast<int>(moves.size()))
                continue;
            const float x = inner.x + numberWidth + static_cast<float>(column) * columnWidth;
            if (ply == static_cast<int>(moves.size()) - 1)
                p.Fill({x - 6 * scale, y + 1 * scale, columnWidth - 6 * scale, rowHeight - 2 * scale}, 7 * scale, ui::Fade(accent, 0.28f));
            p.Text(FontId::Body, x, y + 3 * scale, moves[ply].san, TextColor);
        }
    }
    if (maxScroll > 0) {
        // A thin scroll bar.
        const float barHeight = inner.height * static_cast<float>(visible) / static_cast<float>(rows);
        const float barY = inner.y + (inner.height - barHeight) * (m_moveScroll / maxScroll);
        p.Fill({area.Right() - 5 * scale, barY, 3 * scale, barHeight}, 1.5f * scale, Rgba(255, 255, 255, 50));
    }
}

void App::DrawPanel()
{
    if (m_panelSlide <= 0.001f)
        return;
    const float scale = Scale();
    ui::Painter& p = *m_painter;
    const Rect panel = PanelRect();
    if (panel.Contains(m_pointer.x, m_pointer.y) && (m_pointer.leftPressed || m_pointer.rightPressed))
        m_pointer.consumed = true;
    p.Shadow(panel, 22 * scale, Rgba(0, 0, 0, 70));
    p.Fill(panel, 18 * scale, Rgba(16, 18, 26, 120));
    p.Outline(panel, 18 * scale, Rgba(255, 255, 255, 24));

    const float pad = 20 * scale;
    const float x = panel.x + pad, width = panel.width - 2 * pad;
    float y = panel.y + pad;

    p.Text(FontId::Heading, x, y, T(Str::Title), TextColor);
    if (Button(HelpButton, {panel.Right() - pad - 30 * scale, y - 2 * scale, 30 * scale, 30 * scale}, "?"))
        m_helpVisible = !m_helpVisible;
    y += 44 * scale;

    DrawStatus({x, y, width, 62 * scale});
    y += 62 * scale + 14 * scale;

    // The players, and who is ahead in material.
    int material = 0;
    for (int square = 0; square < 64; ++square) {
        const chess::Piece piece = m_game.Current().At(square);
        material += (piece.color == Color::White ? 1 : -1) * MaterialValue(piece.type);
    }
    for (Color side : {Color::White, Color::Black}) {
        const float rowHeight = 28 * scale;
        p.Disc(x + 9 * scale, y + rowHeight / 2, 7 * scale, Rgba(120, 125, 140));
        p.Disc(x + 9 * scale, y + rowHeight / 2, 6 * scale, side == Color::White ? Rgba(238, 232, 220) : Rgba(22, 22, 26));
        p.Text(FontId::Body, x + 26 * scale, y + (rowHeight - p.LineHeight(FontId::Body)) / 2, std::format("{}  ·  {}", T(side == Color::White ? Str::White : Str::Black), PlayerName(side)),
               TextColor);
        const int lead = side == Color::White ? material : -material;
        if (lead > 0) {
            const std::string text = std::format("+{}", lead);
            p.Text(FontId::Body, x + width - p.Measure(FontId::Body, text), y + (rowHeight - p.LineHeight(FontId::Body)) / 2, text,
                   ui::FromLinear(Themes()[m_theme].accentWarm));
        }
        y += rowHeight;
    }
    y += 12 * scale;

    // From the bottom up: the footer, the settings and the buttons; the moves take the space between.
    float bottom = panel.Bottom() - pad;
    const float footerHeight = p.LineHeight(FontId::Body);
    p.TextCentered(FontId::Body, {x, bottom - footerHeight, width, footerHeight}, T(Str::Footer), ui::Fade(MutedColor, 0.8f));
    bottom -= footerHeight + 14 * scale;

    const float rowHeight = 34 * scale, gap = 8 * scale;
    const float half = (width - gap) / 2;
    bottom -= rowHeight;
    if (Button(SoundButton, {x, bottom, half, rowHeight}, std::format("{}: {}", T(Str::Sound), T(m_sound.enabled ? Str::On : Str::Off)))) {
        m_sound.enabled = !m_sound.enabled;
        Save();
    }
    if (Button(LanguageButton, {x + half + gap, bottom, half, rowHeight}, T(Str::LanguageName))) {
        m_language = m_language == Language::English ? Language::Russian : Language::English;
        Save();
    }
    bottom -= gap;

    const std::array<std::string_view, 4> modes{T(Str::ModeWhite), T(Str::ModeBlack), T(Str::ModeTwoPlayers), T(Str::ModeDemo)};
    const Theme& theme = Themes()[m_theme];
    struct Choice {
        int id;
        Str label;
        std::string value;
    };
    const std::array<Choice, 3> choices{{
        {ThemeSelector, Str::Theme, std::string(m_language == Language::Russian ? theme.nameRussian : theme.nameEnglish)},
        {LevelSelector, Str::Level, std::format("{} · {}", m_level, T(static_cast<Str>(std::to_underlying(Str::Level1) + m_level - 1)))},
        {ModeSelector, Str::Mode, std::string(modes[m_mode])},
    }};
    for (const Choice& choice : choices) {
        bottom -= rowHeight;
        if (const int change = Selector(choice.id, {x, bottom, width, rowHeight}, T(choice.label), choice.value); change != 0) {
            if (choice.id == ThemeSelector)
                SetTheme(m_theme + change);
            else if (choice.id == LevelSelector)
                SetLevel(std::clamp(m_level + change, 1, 5));
            else
                SetMode(m_mode + change);
        }
        bottom -= 6 * scale;
    }
    bottom -= 8 * scale;

    const float buttonHeight = 38 * scale;
    bottom -= buttonHeight;
    if (Button(NewGameButton, {x, bottom, half, buttonHeight}, T(Str::NewGame)))
        NewGame();
    if (Button(FlipButton, {x + half + gap, bottom, half, buttonHeight}, T(Str::Flip)))
        m_camera.Flip();
    bottom -= gap + buttonHeight;
    const float third = (width - 2 * gap) / 3;
    if (Button(UndoButton, {x, bottom, third, buttonHeight}, T(Str::Undo), m_game.CanUndo()))
        Undo();
    if (Button(RedoButton, {x + third + gap, bottom, third, buttonHeight}, T(Str::Redo), m_game.CanRedo()))
        Redo();
    if (Button(HintButton, {x + 2 * (third + gap), bottom, third, buttonHeight}, T(Str::Hint), HumanToMove() && !m_hintThinking, true))
        RequestHint();
    bottom -= 16 * scale;

    p.Text(FontId::Body, x, y, T(Str::Moves), MutedColor);
    y += p.LineHeight(FontId::Body) + 6 * scale;
    if (bottom - y > 40 * scale)
        DrawMoveList({x, y, width, bottom - y});
}

void App::DrawBanner()
{
    if (m_bannerFade <= 0.001f)
        return;
    const float scale = Scale();
    ui::Painter& p = *m_painter;
    const chess::GameStatus status = m_game.Status();
    const std::string_view title = status.reason == chess::EndReason::Checkmate ? T(Str::Checkmate)
                                   : status.reason == chess::EndReason::Stalemate ? T(Str::Stalemate)
                                                                                  : T(Str::Draw);
    const std::string_view detail = status.result == chess::GameResult::WhiteWins   ? T(Str::WhiteWins)
                                    : status.result == chess::GameResult::BlackWins ? T(Str::BlackWins)
                                    : status.reason == chess::EndReason::Repetition ? T(Str::ByRepetition)
                                    : status.reason == chess::EndReason::FiftyMoves ? T(Str::ByFiftyMoves)
                                    : status.reason == chess::EndReason::InsufficientMaterial ? T(Str::ByMaterial)
                                                                                              : std::string_view{};
    const Rect card = BannerCard();
    p.Shadow(card, 24 * scale, ui::Fade(Rgba(0, 0, 0, 120), m_bannerFade));
    p.Fill(card, 18 * scale, ui::Fade(Rgba(14, 16, 24, 150), m_bannerFade));
    p.Outline(card, 18 * scale, ui::Fade(ui::FromLinear(Themes()[m_theme].accent), 0.6f * m_bannerFade));
    p.TextCentered(FontId::Display, {card.x, card.y + 8 * scale, card.width, 48 * scale}, title, ui::Fade(TextColor, m_bannerFade));
    p.TextCentered(FontId::Body, {card.x, card.y + 54 * scale, card.width, 26 * scale}, detail, ui::Fade(MutedColor, m_bannerFade));
}

void App::DrawToasts()
{
    const float scale = Scale();
    ui::Painter& p = *m_painter;
    const Rect free = FreeArea();
    float y = static_cast<float>(m_height) - 40 * scale;
    for (auto toast = m_toasts.rbegin(); toast != m_toasts.rend(); ++toast) {
        const float fade = std::min({1.0f, toast->age * 6, (2.8f - toast->age) * 3});
        const float width = p.Measure(FontId::Body, toast->text) + 40 * scale, height = 36 * scale;
        const Rect pill{free.x + (free.width - width) / 2, y - height, width, height};
        p.Shadow(pill, 14 * scale, ui::Fade(Rgba(0, 0, 0, 110), fade));
        p.Fill(pill, height / 2, ui::Fade(Rgba(20, 22, 30, 225), fade));
        p.TextCentered(FontId::Body, pill, toast->text, ui::Fade(TextColor, fade));
        y -= height + 8 * scale;
    }
}

void App::DrawPromotion()
{
    const float scale = Scale();
    ui::Painter& p = *m_painter;
    const Color side = m_game.Current().SideToMove();
    p.Fill({0, 0, static_cast<float>(m_width), static_cast<float>(m_height)}, 0, Rgba(0, 0, 0, 90));
    if (m_pointer.leftPressed || m_pointer.rightPressed)
        m_pointer.consumed = true;

    const float tile = 116 * scale, gap = 14 * scale, pad = 22 * scale;
    const Rect card = PromotionCard();
    p.Shadow(card, 26 * scale, Rgba(0, 0, 0, 140));
    p.Fill(card, 20 * scale, Rgba(18, 20, 28, 170));
    p.Outline(card, 20 * scale, Rgba(255, 255, 255, 30));
    p.TextCentered(FontId::Heading, {card.x, card.y + 12 * scale, card.width, 36 * scale}, T(Str::PromoteTo), TextColor);

    constexpr std::array<PieceType, 4> types{PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};
    constexpr std::array<const char*, 4> keys{"Q", "R", "B", "N"};
    std::array<Rect, 4> tiles;
    std::optional<chess::Move> chosen;
    for (int i = 0; i < 4; ++i) {
        tiles[i] = {card.x + pad + static_cast<float>(i) * (tile + gap), card.y + pad + 36 * scale, tile, tile};
        if (Button(PromotionTile + i, tiles[i], ""))
            for (const chess::Move& move : *m_promotion)
                if (move.promotion == types[i])
                    chosen = move;
    }
    p.Flush();
    for (int i = 0; i < 4; ++i) {
        const float inset = 10 * scale;
        m_renderer->DrawPieceIcon(types[i], side, tiles[i].x + inset, tiles[i].y + inset, tiles[i].width - 2 * inset, m_time * 0.9f + static_cast<float>(i) * 0.6f);
    }
    for (int i = 0; i < 4; ++i)
        p.Text(FontId::Body, tiles[i].x + 8 * scale, tiles[i].y + 6 * scale, keys[i], MutedColor);
    if (chosen)
        PlayMove(*chosen);
}

void App::DrawHelp()
{
    const float scale = Scale();
    ui::Painter& p = *m_painter;
    p.Fill({0, 0, static_cast<float>(m_width), static_cast<float>(m_height)}, 0, Rgba(0, 0, 0, 110));

    struct Line {
        std::string_view keys;
        Str action;
    };
    const std::array<Line, 20> lines{{
        {T(Str::KeyClick), Str::HelpClick},
        {T(Str::KeyDrag), Str::HelpDrag},
        {T(Str::KeyRightDrag), Str::HelpRotate},
        {T(Str::KeyWheel), Str::HelpZoom},
        {"Ctrl+Z  ←  Backspace", Str::HelpUndo},
        {"Ctrl+Y  →", Str::HelpRedo},
        {"H", Str::HelpHint},
        {"N", Str::HelpNew},
        {"F", Str::HelpFlip},
        {"V", Str::HelpTopView},
        {"1 – 5", Str::HelpLevel},
        {"M", Str::HelpMode},
        {"T", Str::HelpTheme},
        {"S", Str::HelpSound},
        {"L", Str::HelpLanguage},
        {"Tab", Str::HelpPanel},
        {"F11  Alt+Enter", Str::HelpFullScreen},
        {"Ctrl+C", Str::HelpCopy},
        {"Ctrl+V", Str::HelpPaste},
        {"Esc  F1", Str::HelpClose},
    }};
    const float rowHeight = 27 * scale, keysWidth = 200 * scale, pad = 28 * scale;
    const Rect card = HelpCard();
    p.Shadow(card, 26 * scale, Rgba(0, 0, 0, 140));
    p.Fill(card, 20 * scale, Rgba(18, 20, 28, 175));
    p.Outline(card, 20 * scale, Rgba(255, 255, 255, 30));
    p.Text(FontId::Heading, card.x + pad, card.y + pad - 4 * scale, T(Str::Controls), TextColor);
    float y = card.y + pad + 44 * scale;
    for (const Line& line : lines) {
        p.Text(FontId::Body, card.x + pad, y, line.keys, ui::FromLinear(Themes()[m_theme].accent));
        p.Text(FontId::Body, card.x + pad + keysWidth, y, T(line.action), TextColor);
        y += rowHeight;
    }
    if (Button(HelpClose, {card.Right() - pad - 30 * scale, card.y + pad - 8 * scale, 30 * scale, 30 * scale}, "×") ||
        (m_pointer.leftPressed && !card.Contains(m_pointer.x, m_pointer.y)))
        m_helpVisible = false;
    if (m_pointer.leftPressed || m_pointer.rightPressed)
        m_pointer.consumed = true;
}
