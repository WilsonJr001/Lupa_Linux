#pragma once

#include "IMagnifier.h"
#include "LupaTypes.h"
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace LupaLinux {

// Lupa em tela cheia para X11 (equivalente a FullscreenMagnifier.cs).
//
// Funciona como um mini-compositor: em vez de capturar a tela raiz (o que
// incluiria a própria janela da lupa e geraria um "efeito túnel"), cada janela
// de nível superior é obtida via XComposite (XCompositeNameWindowPixmap) e
// ligada a uma textura OpenGL com GLX_EXT_texture_from_pixmap (sem cópia pela
// CPU). As janelas são desenhadas em ordem de empilhamento num framebuffer
// fora da tela, excluindo a janela da lupa, e depois esse framebuffer é
// desenhado ampliado e com a matriz de cor numa janela override-redirect que
// cobre a tela e não recebe entrada (click-through).
//
// Threads: initialise() pode ser chamado de qualquer thread (cria só a parte
// X11). Todo o OpenGL é criado sob demanda e usado apenas na thread que chama
// render() (a thread do UpdateLoop), que também deve chamar shutdown().
class MagnifierLinux : public IMagnifier {
public:
    MagnifierLinux();
    ~MagnifierLinux() override;

    void initialise() override;
    void update(float zoomFactor, float x, float y) override;
    void updateColour(const std::array<float, 25>& colourArray) override;
    void setReadingOverlay(const ReadingOverlay& overlay) override;
    void render() override;
    void shutdown() override;

    // Posição atual do ponteiro (consulta ao servidor X). Usado pela Camera,
    // que no original lia Cursor.Position diretamente.
    Point2D queryPointer();

    // Monitores (XRandR) em coordenadas da tela virtual e o índice do principal
    struct Monitors {
        std::vector<Rect2D> rects;
        int primary = 0;
    };
    Monitors queryMonitors();
    // Chamado na thread do loop quando a disposição dos monitores muda
    void setMonitorsChangedCallback(std::function<void(const Monitors&)> cb) { _monitorsChanged = std::move(cb); }
    // Move o ponteiro (Cursor.Position = ... no original)
    void warpPointer(int x, int y);

    int getWidth() const { return _width; }
    int getHeight() const { return _height; }
    bool isReady() const { return _display != nullptr; }

    // Handler global de erros X11: ignora erros assíncronos (janelas que
    // somem entre uma consulta e outra são normais) e os conta, para que o
    // compositor saiba quando uma criação de pixmap falhou.
    static void installErrorHandler();

private:
    struct PixmapFormat {
        GLXFBConfig config = nullptr;
        bool rgba = false;
        bool yInverted = false;
        bool valid = false;
    };

    struct TrackedWindow {
        int x = 0, y = 0, width = 0, height = 0, border = 0;
        int depth = 0;
        bool viewable = false;
        bool inputOnly = false;
        bool attrsDirty = true;   // precisa reler XGetWindowAttributes
        bool pixmapDirty = true;  // precisa recriar o pixmap (mapeou/redimensionou)
        Pixmap pixmap = 0;
        GLXPixmap glxPixmap = 0;
        GLuint texture = 0;
        bool yInverted = false;
        bool opaque = true;
        XID damage = 0;           // XDamage do conteúdo (0 = sem acompanhamento)
        // Janela do aplicativo dentro da moldura do gerenciador de janelas e se
        // ela está escondida (minimizada ou em outra área de trabalho)
        Window client = 0;
        bool hidden = false;
        bool hiddenDirty = true;
    };

    bool initGL();
    bool initShaders();
    void initFramebuffer();
    void destroyGL();

    const PixmapFormat& pixmapFormatForDepth(int depth);
    void processEvents();
    void refreshWindowList();
    void refreshWindow(Window w, TrackedWindow& tw);
    void releaseWindowPixmap(TrackedWindow& tw);
    void refreshBackground();
    void releaseBackground();
    Window findClient(Window w, int depth);
    long readCardinal(Window w, Atom property, long fallback);
    bool isHidden(TrackedWindow& tw);
    void trackDamage(Drawable drawable, TrackedWindow& tw);
    void forgetDamage(TrackedWindow& tw, bool drawableAlive);
    void handleDamage(const XEvent& ev);
    bool viewIntersects(int x, int y, int width, int height) const;
    void updateCursorImage();
    void setVisible(bool visible);
    void handleRootResize(int width, int height);

    void drawScene(float viewX, float viewY, float viewW, float viewH, const ReadingOverlay& reading,
                   Point2D pointer);
    void drawReadingOverlay(const ReadingOverlay& reading);
    void drawMagnified(float zoom, float viewX, float viewY, const std::array<float, 25>& colour);

    Display* _display = nullptr;
    int _screen = 0;
    Window _root = 0;
    Window _overlayWindow = 0;
    GLXFBConfig _windowConfig = nullptr;
    GLXContext _glContext = nullptr;
    GLXWindow _glxWindow = 0;

    int _width = 1920;
    int _height = 1080;

    bool _hasComposite = false;
    bool _hasFixes = false;
    bool _hasRandr = false;
    bool _hasDamage = false;
    int _damageEventBase = 0;
    std::function<void(const Monitors&)> _monitorsChanged;
    int _fixesEventBase = 0;

    // Extensão texture_from_pixmap
    PFNGLXBINDTEXIMAGEEXTPROC _bindTexImage = nullptr;
    PFNGLXRELEASETEXIMAGEEXTPROC _releaseTexImage = nullptr;
    std::map<int, PixmapFormat> _pixmapFormats;

    // Janelas de nível superior em ordem de empilhamento (de baixo para cima)
    std::unordered_map<Window, TrackedWindow> _windows;
    std::vector<Window> _stack;
    bool _stackDirty = true;

    // Fundo da área de trabalho (_XROOTPMAP_ID / ESETROOT_PMAP_ID). O pixmap é
    // de quem definiu o papel de parede; a lupa só cria a textura em cima dele.
    Atom _atomRootPmap = 0;
    Atom _atomEsetroot = 0;
    Atom _atomWmState = 0;
    Atom _atomNetWmState = 0;
    Atom _atomNetWmStateHidden = 0;
    Atom _atomNetWmDesktop = 0;
    Atom _atomNetCurrentDesktop = 0;
    long _currentDesktop = -1;            // -1: precisa ler
    TrackedWindow _background;
    bool _backgroundDirty = true;

    // Recursos OpenGL
    bool _glReady = false;
    bool _glFailed = false;
    GLuint _quadVAO = 0;
    GLuint _quadVBO = 0;
    GLuint _sceneFBO = 0;
    GLuint _sceneTexture = 0;
    GLuint _windowProgram = 0;
    GLuint _magnifyProgram = 0;
    GLuint _readingProgram = 0;
    GLuint _cursorTexture = 0;

    // Uniforms do passo 1 (janelas -> framebuffer da cena)
    GLint _locWinRect = -1;
    GLint _locWinScreen = -1;
    GLint _locWinFlipY = -1;
    GLint _locWinOpaque = -1;
    GLint _locWinTexture = -1;

    // Uniforms do passo 2 (cena -> tela, ampliada e com cor)
    GLint _locMagScreen = -1;
    GLint _locMagView = -1;
    GLint _locMagZoom = -1;
    GLint _locMagTexture = -1;
    GLint _locMagColorMatrix = -1;
    GLint _locMagColorOffset = -1;

    // Uniforms da camada de leitura (desenhada na cena, passo 1)
    GLint _locReadScreen = -1;
    GLint _locReadOpacity = -1;
    GLint _locReadBack = -1;
    GLint _locReadBand = -1;
    GLint _locReadArea = -1;
    GLint _locReadLines = -1;
    GLint _locReadLinesBase = -1;
    GLint _locReadLineHeight = -1;
    GLint _locReadLineOpacity = -1;
    GLint _locReadLineColour = -1;
    GLint _locReadDouble = -1;

    // Cursor (desenhado dentro da cena, para ser ampliado junto)
    bool _cursorDirty = true;
    int _cursorWidth = 0;
    int _cursorHeight = 0;
    int _cursorHotX = 0;
    int _cursorHotY = 0;
    bool _cursorHidden = false;

    bool _visible = false;

    // Redesenho só quando algo muda (XDamage). _sceneDirty é ligado por dano
    // dentro da área visível, mudanças de janelas/cursor/papel de parede e
    // Expose da lupa; mudanças de zoom/posição/cor/leitura/ponteiro são
    // detectadas comparando com o último quadro desenhado.
    bool _sceneDirty = true;
    int _drawnArea[4] = {0, 0, 0, 0};   // área da cena desenhada: x, y, x2, y2
    float _drawnZoom = 0.0f;
    float _drawnViewX = 0.0f;
    float _drawnViewY = 0.0f;
    std::array<float, 25> _drawnColour{};
    ReadingOverlay _drawnReading;
    Point2D _drawnPointer{-1, -1};
    std::chrono::steady_clock::time_point _lastDraw{};

    // Estado compartilhado com as outras threads (protegido por _stateMutex)
    std::mutex _stateMutex;
    float _zoomFactor = 1.0f;
    float _viewX = 0.0f;
    float _viewY = 0.0f;
    std::array<float, 25> _colourArray{};
    ReadingOverlay _reading;

    static std::atomic<unsigned long> s_xErrorCount;
};

} // namespace LupaLinux
