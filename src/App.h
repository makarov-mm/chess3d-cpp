#pragma once

#include "BoardView.h"
#include "Camera.h"
#include "Chess.h"
#include "Engine.h"
#include "Gl.h"
#include "Renderer.h"
#include "Settings.h"
#include "Sound.h"
#include "Strings.h"
#include "Text.h"
#include "UiPainter.h"
#include "Win32.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The game's window: it runs the game, the computer players and the interface, and draws them.
class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();

private:
    enum Mode { PlayWhite, PlayBlack, TwoPlayers, Demo };

    // What the mouse did since the last frame.
    struct Pointer {
        float x = -1, y = -1;
        bool leftDown = false, rightDown = false;
        bool leftPressed = false, leftReleased = false, rightPressed = false, rightReleased = false;
        float wheel = 0;
        bool consumed = false; // taken by the interface
    };

    enum class Gesture { None, PressPiece, DragPiece, TurnCamera };

    struct Toast {
        std::string text;
        float age = 0;
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateMainWindow();
    void ShowLoading();
    void BuildFonts();
    void Frame();
    void Update(float seconds);
    bool NeedsAnimation() const;

    // The game.
    bool IsHuman(chess::Color side) const;
    bool HumanToMove() const;
    void NewGame();
    void PlayMove(chess::Move move);
    void Undo();
    void Redo();
    void SetMode(int mode);
    void SetLevel(int level);
    void SetTheme(int theme);
    void StopComputer();
    void UpdateComputer(float seconds);
    void RequestHint();
    void AfterPositionChange();
    void Announce(const chess::MoveRecord& record);
    void ClearSelection();
    void Select(int square);
    void TryMove(int from, int to);
    void CopyGame(bool fen);
    void PasteGame();
    void Save();

    // Input.
    void HandleBoardPointer();
    void HandleKey(WPARAM key);
    int SquareUnderPointer() const;
    math::Vec3 PointerOnPlane(float height) const;

    // The interface.
    float Scale() const { return static_cast<float>(m_dpi) / 96.0f; }
    ui::Rect PanelRect() const;
    ui::Rect FreeArea() const;
    std::vector<render::FrostedPanel> FrostedPanels() const;
    ui::Rect HelpCard() const;
    ui::Rect PromotionCard() const;
    ui::Rect BannerCard() const;
    void DrawInterface();
    void DrawPanel();
    void DrawMoveList(ui::Rect area);
    void DrawStatus(ui::Rect area);
    void DrawBanner();
    void DrawPromotion();
    void DrawHelp();
    void DrawToasts();
    bool Button(int id, ui::Rect rect, std::string_view label, bool enabled = true, bool accent = false);
    // A label with a value stepped by two arrows; returns -1 or 1 when one is clicked, else 0.
    int Selector(int id, ui::Rect rect, std::string_view label, std::string_view value);
    void ShowToast(std::string text);
    std::string_view T(Str id) const { return Tr(id, m_language); }
    std::string PlayerName(chess::Color side) const;

    void ToggleFullScreen();

    HINSTANCE m_instance;
    HWND m_window = nullptr;
    std::unique_ptr<gl::Context> m_context;
    std::unique_ptr<render::Renderer> m_renderer;
    std::unique_ptr<ui::Painter> m_painter;
    std::array<std::unique_ptr<text::Font>, 3> m_fonts;
    std::unique_ptr<BoardView> m_view;
    SoundBank m_sound;
    Settings m_settings;
    Language m_language = Language::English;
    int m_width = 0, m_height = 0;
    UINT m_dpi = 96;
    UINT m_fontDpi = 0;
    bool m_running = true;
    bool m_dirty = true; // something to draw since the last frame
    bool m_fullScreen = false;
    WINDOWPLACEMENT m_placement{sizeof(WINDOWPLACEMENT)};
    std::chrono::steady_clock::time_point m_lastFrame;
    float m_time = 0;

    chess::Game m_game;
    OrbitCamera m_camera;
    int m_mode = PlayWhite;
    int m_level = 3;
    int m_theme = 0;
    chess::AiPlayer m_computer{32};
    chess::AiPlayer m_hinter{16};
    bool m_computerThinking = false;
    float m_computerClock = 0;
    std::optional<chess::SearchResult> m_computerResult;
    bool m_hintThinking = false;
    std::optional<chess::Move> m_hint;
    float m_hintGlow = 0;
    bool m_announcedEnd = false;

    Pointer m_pointer;
    Gesture m_gesture = Gesture::None;
    float m_pressX = 0, m_pressY = 0;
    float m_dragX = 0, m_dragY = 0; // the pointer when the camera last turned
    int m_pressSquare = -1;
    int m_selected = -1;
    std::vector<chess::Move> m_selectedMoves;
    int m_hoverPiece = -1;
    int m_hoverSquare = -1;
    std::optional<std::vector<chess::Move>> m_promotion; // the choices, while the player picks one
    bool m_cursorHand = false;

    int m_activeWidget = 0;
    bool m_panelVisible = true;
    float m_panelSlide = 1;
    bool m_helpVisible = false;
    float m_moveScroll = 0;
    bool m_followMoves = true;
    float m_bannerFade = 0;
    std::vector<Toast> m_toasts;
};
