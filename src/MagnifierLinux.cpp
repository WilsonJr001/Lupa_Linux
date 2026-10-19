#define GL_GLEXT_PROTOTYPES
#include "MagnifierLinux.h"
#include "XExtCompat.h"
#include <GL/glext.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace LupaLinux {

std::atomic<unsigned long> MagnifierLinux::s_xErrorCount{0};

// --------------------------------------------------------------------------
// Shaders
// --------------------------------------------------------------------------

// Passo 1: desenha uma janela (ou o cursor) na cena, em coordenadas de tela.
static const char* windowVertexSrc = R"(
#version 130
in vec2 a_pos;               // quad unitário, (0,0) = canto superior esquerdo
uniform vec4 u_rect;         // x, y, largura, altura em pixels de tela
uniform vec2 u_screen;
uniform int u_flipY;
out vec2 v_uv;
void main() {
    vec2 p = u_rect.xy + a_pos * u_rect.zw;
    gl_Position = vec4(p.x / u_screen.x * 2.0 - 1.0, 1.0 - p.y / u_screen.y * 2.0, 0.0, 1.0);
    v_uv = vec2(a_pos.x, u_flipY == 1 ? 1.0 - a_pos.y : a_pos.y);
}
)";

static const char* windowFragmentSrc = R"(
#version 130
uniform sampler2D u_texture;
uniform int u_opaque;
in vec2 v_uv;
out vec4 fragColor;
void main() {
    vec4 c = texture(u_texture, v_uv);
    if (u_opaque == 1) c.a = 1.0;
    fragColor = c;
}
)";

// Passo 2: amplia a cena e aplica a matriz de cor (equivalente a
// MagSetFullscreenTransform + MagSetFullscreenColorEffect do Windows).
static const char* magnifyVertexSrc = R"(
#version 130
in vec2 a_pos;
out vec2 v_screenUV;         // (0,0) = canto superior esquerdo da tela
void main() {
    v_screenUV = a_pos;
    gl_Position = vec4(a_pos.x * 2.0 - 1.0, 1.0 - a_pos.y * 2.0, 0.0, 1.0);
}
)";

static const char* magnifyFragmentSrc = R"(
#version 130
uniform sampler2D u_scene;
uniform vec2 u_screen;
uniform vec2 u_view;         // canto superior esquerdo da área ampliada (pixels)
uniform float u_zoom;
uniform mat3 u_colorMatrix;
uniform vec3 u_colorOffset;
in vec2 v_screenUV;
out vec4 fragColor;
void main() {
    vec2 src = u_view + v_screenUV * u_screen / u_zoom;
    vec2 uv = vec2(src.x / u_screen.x, 1.0 - src.y / u_screen.y);
    vec3 col = texture(u_scene, uv).rgb;

    // Matriz de cor no formato do Windows (vetor linha * matriz 5x5)
    col = clamp(u_colorMatrix * col + u_colorOffset, 0.0, 1.0);
    fragColor = vec4(col, 1.0);
}
)";

// Camada do modo leitura (Reading.cs), desenhada na cena em pixels de tela:
// fundo semitransparente, faixa transparente acima do cursor e linhas-guia.
static const char* readingFragmentSrc = R"(
#version 130
uniform vec2 u_screen;
uniform float u_opacity;
uniform vec3 u_back;
uniform vec4 u_area;          // janela de leitura: o monitor onde estava o cursor
uniform vec4 u_band;          // x, y, largura, altura
uniform int u_lines;
uniform float u_linesBase;
uniform float u_lineHeight;
uniform float u_lineOpacity;  // 0..255
uniform vec3 u_lineColour;
uniform int u_double;
out vec4 fragColor;
void main() {
    vec2 p = vec2(gl_FragCoord.x, u_screen.y - gl_FragCoord.y);
    bool outside = p.x < u_area.x || p.x >= u_area.x + u_area.z || p.y < u_area.y || p.y >= u_area.y + u_area.w;
    if (outside || (p.x >= u_band.x && p.x < u_band.x + u_band.z && p.y >= u_band.y && p.y < u_band.y + u_band.w)) {
        fragColor = vec4(0.0);  // TransparencyKey do FocusPanel
        return;
    }
    vec3 c = u_back;
    if (u_lines == 1) {
        float row = floor(p.y);
        for (int i = 1; i < 12; ++i) {
            float ly = floor(u_linesBase + u_lineHeight * float(i));
            if (row == ly || (u_double == 1 && row == ly + 1.0)) {
                float alpha = floor(u_lineOpacity / pow(1.25, float(i))) / 255.0;
                c = mix(c, u_lineColour, alpha);
            }
        }
    }
    fragColor = vec4(c * u_opacity, u_opacity);  // pré-multiplicado
}
)";

static bool sameReading(const ReadingOverlay& a, const ReadingOverlay& b) {
    return a.visible == b.visible && a.opacity == b.opacity && std::equal(a.back, a.back + 3, b.back) &&
           a.areaX == b.areaX && a.areaY == b.areaY && a.areaWidth == b.areaWidth &&
           a.areaHeight == b.areaHeight && a.bandX == b.bandX && a.bandY == b.bandY &&
           a.bandWidth == b.bandWidth && a.bandHeight == b.bandHeight && a.linesVisible == b.linesVisible &&
           a.linesBaseY == b.linesBaseY && a.lineHeight == b.lineHeight && a.lineOpacity == b.lineOpacity &&
           std::equal(a.lineColour, a.lineColour + 3, b.lineColour) && a.doubleLines == b.doubleLines;
}

static GLuint compileShader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[MagnifierLinux] Erro de shader: " << log << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint linkProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::cerr << "[MagnifierLinux] Erro ao ligar programa: " << log << "\n";
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// --------------------------------------------------------------------------
// Ciclo de vida
// --------------------------------------------------------------------------

MagnifierLinux::MagnifierLinux() {
    _colourArray[0] = _colourArray[6] = _colourArray[12] = _colourArray[18] = _colourArray[24] = 1.0f;
}

MagnifierLinux::~MagnifierLinux() {
    shutdown();
}

void MagnifierLinux::installErrorHandler() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    XSetErrorHandler([](Display*, XErrorEvent*) -> int {
        s_xErrorCount++;
        return 0;
    });
}

void MagnifierLinux::initialise() {
    if (_display) return;

    _display = XOpenDisplay(nullptr);
    if (!_display) {
        std::cerr << "[MagnifierLinux] Erro: não foi possível abrir o display X11.\n";
        return;
    }
    _screen = DefaultScreen(_display);
    _root = RootWindow(_display, _screen);
    _width = DisplayWidth(_display, _screen);
    _height = DisplayHeight(_display, _screen);

    int evBase = 0, errBase = 0, major = 0, minor = 0;
    _hasComposite = XCompositeQueryExtension(_display, &evBase, &errBase);
    if (_hasComposite) {
        major = 0; minor = 4;
        XCompositeQueryVersion(_display, &major, &minor);
        _hasComposite = (major > 0 || minor >= 2); // NameWindowPixmap exige 0.2
    }
    if (!_hasComposite) {
        std::cerr << "[MagnifierLinux] Erro: extensão Composite indisponível; a lupa não funcionará.\n";
    }

    _hasFixes = XFixesQueryExtension(_display, &_fixesEventBase, &errBase);
    if (_hasFixes) {
        major = 4; minor = 0;
        XFixesQueryVersion(_display, &major, &minor); // obrigatório antes de usar XFixes
        XFixesSelectCursorInput(_display, _root, XFixesDisplayCursorNotifyMask);
    }

    // Sem XDamage a lupa continua funcionando, mas redesenha todo quadro
    _hasDamage = XDamageQueryExtension(_display, &_damageEventBase, &errBase);
    if (_hasDamage) {
        major = 1; minor = 1;
        XDamageQueryVersion(_display, &major, &minor);
    }

    // Config GLX da janela da lupa (precisa existir antes de criar a janela)
    static const int fbAttribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_DOUBLEBUFFER, True,
        None
    };
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(_display, _screen, fbAttribs, &count);
    XVisualInfo* vi = nullptr;
    for (int i = 0; i < count && !vi; ++i) {
        XVisualInfo* candidate = glXGetVisualFromFBConfig(_display, configs[i]);
        if (candidate && candidate->depth == 24) {
            _windowConfig = configs[i];
            vi = candidate;
        } else if (candidate) {
            XFree(candidate);
        }
    }
    if (configs) XFree(configs);
    if (!vi) {
        std::cerr << "[MagnifierLinux] Erro: nenhum FBConfig GLX compatível.\n";
        return;
    }

    XSetWindowAttributes swa{};
    swa.override_redirect = True;
    swa.colormap = XCreateColormap(_display, _root, vi->visual, AllocNone);
    swa.background_pixel = 0;
    swa.border_pixel = 0;
    _overlayWindow = XCreateWindow(_display, _root, 0, 0, _width, _height, 0,
                                   vi->depth, InputOutput, vi->visual,
                                   CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &swa);
    XFree(vi);
    // Expose: algo cobriu e descobriu a lupa, o conteúdo precisa ser redesenhado
    XSelectInput(_display, _overlayWindow, ExposureMask);

    // Click-through: região de entrada vazia
    XShapeCombineRectangles(_display, _overlayWindow, ShapeInput, 0, 0, nullptr, 0, ShapeSet, 0);

    XStoreName(_display, _overlayWindow, "Lupa Linux");
    // Pede ao compositor do gerenciador de janelas para não compor a lupa
    Atom bypass = XInternAtom(_display, "_NET_WM_BYPASS_COMPOSITOR", False);
    long one = 1;
    XChangeProperty(_display, _overlayWindow, bypass, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&one), 1);

    if (_hasComposite) {
        // Redireciona (modo automático) todas as janelas de nível superior para
        // que tenham um pixmap próprio. Se o gerenciador de janelas já compõe a
        // tela isso não muda nada visível; se não compõe, o servidor continua
        // desenhando as janelas normalmente. A lupa em si não é redirecionada.
        XCompositeRedirectSubwindows(_display, _root, CompositeRedirectAutomatic);
        XCompositeUnredirectWindow(_display, _overlayWindow, CompositeRedirectAutomatic);
    }

    _hasRandr = XRRQueryExtension(_display, &evBase, &errBase);

    // Acompanha mapeamento, redimensionamento e destruição das janelas
    // (Substructure) e mudanças no tamanho da tela virtual, que acontecem
    // quando monitores são ligados/desligados/rearranjados
    // (Structure). PropertyChange avisa quando o papel de parede é trocado.
    _atomRootPmap = XInternAtom(_display, "_XROOTPMAP_ID", False);
    _atomEsetroot = XInternAtom(_display, "ESETROOT_PMAP_ID", False);
    _atomWmState = XInternAtom(_display, "WM_STATE", False);
    _atomNetWmState = XInternAtom(_display, "_NET_WM_STATE", False);
    _atomNetWmStateHidden = XInternAtom(_display, "_NET_WM_STATE_HIDDEN", False);
    _atomNetWmDesktop = XInternAtom(_display, "_NET_WM_DESKTOP", False);
    _atomNetCurrentDesktop = XInternAtom(_display, "_NET_CURRENT_DESKTOP", False);
    XSelectInput(_display, _root, SubstructureNotifyMask | StructureNotifyMask | PropertyChangeMask);
    XFlush(_display);

    std::cout << "[MagnifierLinux] Display " << _width << "x" << _height
              << " | Composite: " << (_hasComposite ? "sim" : "não")
              << " | XFixes: " << (_hasFixes ? "sim" : "não")
              << " | Damage: " << (_hasDamage ? "sim" : "não") << "\n";
}

bool MagnifierLinux::initGL() {
    _glContext = glXCreateNewContext(_display, _windowConfig, GLX_RGBA_TYPE, nullptr, True);
    if (!_glContext) {
        std::cerr << "[MagnifierLinux] Erro: falha ao criar contexto OpenGL.\n";
        return false;
    }
    _glxWindow = glXCreateWindow(_display, _windowConfig, _overlayWindow, nullptr);
    if (!glXMakeContextCurrent(_display, _glxWindow, _glxWindow, _glContext)) {
        std::cerr << "[MagnifierLinux] Erro: glXMakeContextCurrent falhou.\n";
        return false;
    }

    _bindTexImage = reinterpret_cast<PFNGLXBINDTEXIMAGEEXTPROC>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXBindTexImageEXT")));
    _releaseTexImage = reinterpret_cast<PFNGLXRELEASETEXIMAGEEXTPROC>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXReleaseTexImageEXT")));
    const char* exts = glXQueryExtensionsString(_display, _screen);
    if (!_bindTexImage || !_releaseTexImage || !exts || !std::strstr(exts, "GLX_EXT_texture_from_pixmap")) {
        std::cerr << "[MagnifierLinux] Erro: GLX_EXT_texture_from_pixmap indisponível.\n";
        return false;
    }

    if (!initShaders()) return false;

    // Quad unitário compartilhado pelos dois passos
    static const float quad[] = { 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f };
    glGenVertexArrays(1, &_quadVAO);
    glGenBuffers(1, &_quadVBO);
    glBindVertexArray(_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    initFramebuffer();

    glGenTextures(1, &_cursorTexture);
    glBindTexture(GL_TEXTURE_2D, _cursorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glDisable(GL_DEPTH_TEST);
    std::cout << "[MagnifierLinux] OpenGL: " << glGetString(GL_RENDERER) << "\n";
    return true;
}

bool MagnifierLinux::initShaders() {
    _windowProgram = linkProgram(windowVertexSrc, windowFragmentSrc);
    _magnifyProgram = linkProgram(magnifyVertexSrc, magnifyFragmentSrc);
    _readingProgram = linkProgram(magnifyVertexSrc, readingFragmentSrc);
    if (!_windowProgram || !_magnifyProgram || !_readingProgram) return false;

    _locWinRect = glGetUniformLocation(_windowProgram, "u_rect");
    _locWinScreen = glGetUniformLocation(_windowProgram, "u_screen");
    _locWinFlipY = glGetUniformLocation(_windowProgram, "u_flipY");
    _locWinOpaque = glGetUniformLocation(_windowProgram, "u_opaque");
    _locWinTexture = glGetUniformLocation(_windowProgram, "u_texture");

    _locMagScreen = glGetUniformLocation(_magnifyProgram, "u_screen");
    _locMagView = glGetUniformLocation(_magnifyProgram, "u_view");
    _locMagZoom = glGetUniformLocation(_magnifyProgram, "u_zoom");
    _locMagTexture = glGetUniformLocation(_magnifyProgram, "u_scene");
    _locMagColorMatrix = glGetUniformLocation(_magnifyProgram, "u_colorMatrix");
    _locMagColorOffset = glGetUniformLocation(_magnifyProgram, "u_colorOffset");

    _locReadScreen = glGetUniformLocation(_readingProgram, "u_screen");
    _locReadOpacity = glGetUniformLocation(_readingProgram, "u_opacity");
    _locReadBack = glGetUniformLocation(_readingProgram, "u_back");
    _locReadBand = glGetUniformLocation(_readingProgram, "u_band");
    _locReadArea = glGetUniformLocation(_readingProgram, "u_area");
    _locReadLines = glGetUniformLocation(_readingProgram, "u_lines");
    _locReadLinesBase = glGetUniformLocation(_readingProgram, "u_linesBase");
    _locReadLineHeight = glGetUniformLocation(_readingProgram, "u_lineHeight");
    _locReadLineOpacity = glGetUniformLocation(_readingProgram, "u_lineOpacity");
    _locReadLineColour = glGetUniformLocation(_readingProgram, "u_lineColour");
    _locReadDouble = glGetUniformLocation(_readingProgram, "u_double");
    return true;
}

void MagnifierLinux::initFramebuffer() {
    glGenTextures(1, &_sceneTexture);
    glBindTexture(GL_TEXTURE_2D, _sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &_sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, _sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _sceneTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[MagnifierLinux] Aviso: framebuffer da cena incompleto.\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MagnifierLinux::destroyGL() {
    if (!_glContext) return;
    for (auto& [w, tw] : _windows) {
        forgetDamage(tw, true);
        releaseWindowPixmap(tw);
    }
    _windows.clear();
    releaseBackground();
    _stack.clear();
    if (_glReady) {
        glDeleteTextures(1, &_cursorTexture);
        glDeleteTextures(1, &_sceneTexture);
        glDeleteFramebuffers(1, &_sceneFBO);
        glDeleteBuffers(1, &_quadVBO);
        glDeleteVertexArrays(1, &_quadVAO);
        glDeleteProgram(_windowProgram);
        glDeleteProgram(_magnifyProgram);
        glDeleteProgram(_readingProgram);
    }
    glXMakeContextCurrent(_display, None, None, nullptr);
    if (_glxWindow) glXDestroyWindow(_display, _glxWindow);
    glXDestroyContext(_display, _glContext);
    _glxWindow = 0;
    _glContext = nullptr;
    _glReady = false;
}

void MagnifierLinux::shutdown() {
    if (!_display) return;
    destroyGL();
    if (_cursorHidden && _hasFixes) {
        XFixesShowCursor(_display, _root);
        _cursorHidden = false;
    }
    if (_hasComposite) {
        XCompositeUnredirectSubwindows(_display, _root, CompositeRedirectAutomatic);
    }
    if (_overlayWindow) {
        XDestroyWindow(_display, _overlayWindow);
        _overlayWindow = 0;
    }
    XCloseDisplay(_display);
    _display = nullptr;
    _visible = false;
}

// --------------------------------------------------------------------------
// Estado vindo das outras threads
// --------------------------------------------------------------------------

void MagnifierLinux::update(float zoomFactor, float x, float y) {
    std::lock_guard<std::mutex> lock(_stateMutex);
    _zoomFactor = zoomFactor;
    _viewX = x;
    _viewY = y;
}

void MagnifierLinux::updateColour(const std::array<float, 25>& colourArray) {
    std::lock_guard<std::mutex> lock(_stateMutex);
    _colourArray = colourArray;
}

void MagnifierLinux::setReadingOverlay(const ReadingOverlay& overlay) {
    std::lock_guard<std::mutex> lock(_stateMutex);
    _reading = overlay;
}

MagnifierLinux::Monitors MagnifierLinux::queryMonitors() {
    Monitors result;
    if (_display && _hasRandr) {
        int n = 0;
        XRRMonitorInfo* info = XRRGetMonitors(_display, _root, True, &n);
        for (int i = 0; i < n; ++i) {
            result.rects.emplace_back(info[i].x, info[i].y, info[i].width, info[i].height);
            if (info[i].primary) result.primary = i;
        }
        if (info) XRRFreeMonitors(info);
    }
    if (result.rects.empty()) result.rects.emplace_back(0, 0, _width, _height); // sem XRandR: uma tela só
    return result;
}

void MagnifierLinux::handleRootResize(int width, int height) {
    std::cout << "[MagnifierLinux] Tela virtual agora " << width << "x" << height << "\n";
    _width = width;
    _height = height;
    _sceneDirty = true;
    XResizeWindow(_display, _overlayWindow, width, height);
    if (_glReady) {
        glBindTexture(GL_TEXTURE_2D, _sceneTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
}

Point2D MagnifierLinux::queryPointer() {
    if (!_display) return {0, 0};
    Window rootRet, childRet;
    int rootX = 0, rootY = 0, winX = 0, winY = 0;
    unsigned int mask = 0;
    XQueryPointer(_display, _root, &rootRet, &childRet, &rootX, &rootY, &winX, &winY, &mask);
    return {rootX, rootY};
}

void MagnifierLinux::warpPointer(int x, int y) {
    if (!_display) return;
    XWarpPointer(_display, None, _root, 0, 0, 0, 0, x, y);
    XFlush(_display);
}

// --------------------------------------------------------------------------
// Acompanhamento das janelas
// --------------------------------------------------------------------------

const MagnifierLinux::PixmapFormat& MagnifierLinux::pixmapFormatForDepth(int depth) {
    auto it = _pixmapFormats.find(depth);
    if (it != _pixmapFormats.end()) return it->second;

    PixmapFormat fmt;
    int count = 0;
    GLXFBConfig* configs = glXGetFBConfigs(_display, _screen, &count);
    for (int i = 0; i < count && !fmt.valid; ++i) {
        XVisualInfo* vi = glXGetVisualFromFBConfig(_display, configs[i]);
        if (!vi) continue;
        int vdepth = vi->depth;
        XFree(vi);
        if (vdepth != depth) continue;

        int value = 0;
        glXGetFBConfigAttrib(_display, configs[i], GLX_DRAWABLE_TYPE, &value);
        if (!(value & GLX_PIXMAP_BIT)) continue;
        glXGetFBConfigAttrib(_display, configs[i], GLX_BIND_TO_TEXTURE_TARGETS_EXT, &value);
        if (!(value & GLX_TEXTURE_2D_BIT_EXT)) continue;

        int rgba = 0, rgb = 0;
        glXGetFBConfigAttrib(_display, configs[i], GLX_BIND_TO_TEXTURE_RGBA_EXT, &rgba);
        glXGetFBConfigAttrib(_display, configs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &rgb);
        if (depth == 32 ? !rgba : !(rgb || rgba)) continue;

        int yInverted = 0;
        glXGetFBConfigAttrib(_display, configs[i], GLX_Y_INVERTED_EXT, &yInverted);

        fmt.config = configs[i];
        fmt.rgba = (depth == 32) || !rgb;
        fmt.yInverted = yInverted;
        fmt.valid = true;
    }
    if (configs) XFree(configs);
    if (!fmt.valid) {
        std::cerr << "[MagnifierLinux] Aviso: sem FBConfig de pixmap para profundidade " << depth << ".\n";
    }
    return _pixmapFormats[depth] = fmt;
}

void MagnifierLinux::releaseWindowPixmap(TrackedWindow& tw) {
    if (tw.texture) {
        glDeleteTextures(1, &tw.texture);
        tw.texture = 0;
    }
    if (tw.glxPixmap) {
        glXDestroyPixmap(_display, tw.glxPixmap);
        tw.glxPixmap = 0;
    }
    if (tw.pixmap) {
        XFreePixmap(_display, tw.pixmap);
        tw.pixmap = 0;
    }
    tw.pixmapDirty = true;
}

void MagnifierLinux::processEvents() {
    while (XPending(_display) > 0) {
        XEvent ev;
        XNextEvent(_display, &ev);

        if (_hasFixes && ev.type == _fixesEventBase + XFixesCursorNotify) {
            _cursorDirty = true;
            _sceneDirty = true;
            continue;
        }
        if (_hasDamage && ev.type == _damageEventBase + XDamageNotify) {
            handleDamage(ev);
            continue;
        }

        // Qualquer mudança estrutural (mapear, mover, empilhar...) pede um quadro novo
        if (ev.type != CreateNotify) _sceneDirty = true;

        Window w = 0;
        switch (ev.type) {
            case CreateNotify:
                if (ev.xcreatewindow.parent == _root) _stackDirty = true;
                break;
            case DestroyNotify:
                w = ev.xdestroywindow.window;
                if (auto it = _windows.find(w); it != _windows.end()) {
                    forgetDamage(it->second, false); // o servidor já liberou o Damage
                    releaseWindowPixmap(it->second);
                    _windows.erase(it);
                }
                _stackDirty = true;
                break;
            case ReparentNotify:
                _stackDirty = true;
                break;
            case Expose:
                break; // já marcou _sceneDirty
            case PropertyNotify: {
                const XPropertyEvent& pe = ev.xproperty;
                if (pe.window == _root) {
                    if (pe.atom == _atomRootPmap || pe.atom == _atomEsetroot) _backgroundDirty = true;
                    if (pe.atom == _atomNetCurrentDesktop) {
                        _currentDesktop = -1; // relido em isHidden
                        for (auto& [win, tw] : _windows) tw.hiddenDirty = true;
                    }
                } else if (pe.atom == _atomWmState || pe.atom == _atomNetWmState || pe.atom == _atomNetWmDesktop) {
                    for (auto& [win, tw] : _windows) {
                        if (win == pe.window || tw.client == pe.window) tw.hiddenDirty = true;
                    }
                }
                break;
            }
            case MapNotify:
                w = ev.xmap.window;
                if (w == _overlayWindow) break;
                if (auto it = _windows.find(w); it != _windows.end()) {
                    it->second.attrsDirty = true;
                    it->second.pixmapDirty = true;
                }
                _stackDirty = true;
                // Menus e dicas (override-redirect) aparecem por cima: mantém a lupa no topo
                if (_visible) XRaiseWindow(_display, _overlayWindow);
                break;
            case UnmapNotify:
                w = ev.xunmap.window;
                if (auto it = _windows.find(w); it != _windows.end()) {
                    it->second.viewable = false;
                    releaseWindowPixmap(it->second);
                }
                break;
            case ConfigureNotify: {
                const XConfigureEvent& ce = ev.xconfigure;
                if (ce.window == _overlayWindow) break;
                if (ce.window == _root) {
                    // DisplaySettingsChanged: tela virtual mudou (monitores)
                    if (ce.width != _width || ce.height != _height) handleRootResize(ce.width, ce.height);
                    if (_monitorsChanged) _monitorsChanged(queryMonitors());
                    break;
                }
                if (auto it = _windows.find(ce.window); it != _windows.end()) {
                    TrackedWindow& tw = it->second;
                    if (tw.width != ce.width || tw.height != ce.height || tw.border != ce.border_width) {
                        tw.pixmapDirty = true;
                    }
                    tw.x = ce.x;
                    tw.y = ce.y;
                    tw.width = ce.width;
                    tw.height = ce.height;
                    tw.border = ce.border_width;
                }
                _stackDirty = true; // a ordem de empilhamento pode ter mudado
                if (_visible) XRaiseWindow(_display, _overlayWindow);
                break;
            }
            default:
                break;
        }
    }
}

// --------------------------------------------------------------------------
// Janelas escondidas
//
// Gerenciadores que compõem a tela, como Mutter (GNOME) e Muffin (Cinnamon),
// mantêm mapeadas as janelas minimizadas e as de outras áreas de trabalho (para
// mostrar miniaturas) e só deixam de pintá-las. Sem este filtro elas
// apareceriam na lupa como se estivessem abertas.
// --------------------------------------------------------------------------

// Janela do aplicativo: a que tem WM_STATE (a própria ou uma filha da moldura)
Window MagnifierLinux::findClient(Window w, int depth) {
    Atom type = 0;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(_display, w, _atomWmState, 0, 0, False, AnyPropertyType, &type, &format, &count,
                           &after, &data) == Success) {
        if (data) XFree(data);
        if (type != None) return w;
    }
    if (depth >= 2) return 0;
    Window rootRet = 0, parentRet = 0;
    Window* children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(_display, w, &rootRet, &parentRet, &children, &n)) return 0;
    Window found = 0;
    for (unsigned int i = n; i-- > 0 && !found;) found = findClient(children[i], depth + 1);
    if (children) XFree(children);
    return found;
}

long MagnifierLinux::readCardinal(Window w, Atom property, long fallback) {
    Atom type = 0;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    long value = fallback;
    if (XGetWindowProperty(_display, w, property, 0, 1, False, AnyPropertyType, &type, &format, &count, &after,
                           &data) == Success && data) {
        if (format == 32 && count == 1) value = *reinterpret_cast<long*>(data);
        XFree(data);
    }
    return value;
}

bool MagnifierLinux::isHidden(TrackedWindow& tw) {
    if (!tw.hiddenDirty) return tw.hidden;
    tw.hiddenDirty = false;
    tw.hidden = false;
    if (!tw.client) return false; // menus, dicas e janelas sem gerenciador: sempre visíveis

    // ICCCM: IconicState = 3 (minimizada)
    if (readCardinal(tw.client, _atomWmState, 1) == IconicState) tw.hidden = true;

    // EWMH: _NET_WM_STATE_HIDDEN
    Atom type = 0;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (!tw.hidden && XGetWindowProperty(_display, tw.client, _atomNetWmState, 0, 32, False, XA_ATOM, &type, &format,
                                         &count, &after, &data) == Success && data) {
        const Atom* atoms = reinterpret_cast<const Atom*>(data);
        for (unsigned long i = 0; i < count; ++i) tw.hidden |= (atoms[i] == _atomNetWmStateHidden);
        XFree(data);
    }

    // Outra área de trabalho (0xFFFFFFFF = em todas)
    if (!tw.hidden) {
        if (_currentDesktop == -1) _currentDesktop = readCardinal(_root, _atomNetCurrentDesktop, -2); // -2: não há
        long desktop = readCardinal(tw.client, _atomNetWmDesktop, -3);
        if (_currentDesktop >= 0 && desktop >= 0 && desktop != 0xFFFFFFFFL && desktop != _currentDesktop) {
            tw.hidden = true;
        }
    }
    return tw.hidden;
}

// --------------------------------------------------------------------------
// Danos (XDamage): só redesenha quando algo visível mudou
// --------------------------------------------------------------------------

void MagnifierLinux::trackDamage(Drawable drawable, TrackedWindow& tw) {
    if (!_hasDamage || tw.damage) return;
    tw.damage = XDamageCreate(_display, drawable, XDamageReportBoundingBox);
}

void MagnifierLinux::forgetDamage(TrackedWindow& tw, bool drawableAlive) {
    // Erros (drawable que sumiu antes do aviso) são ignorados pelo handler global
    if (tw.damage && drawableAlive) XDamageDestroy(_display, tw.damage);
    tw.damage = 0;
}

bool MagnifierLinux::viewIntersects(int x, int y, int width, int height) const {
    return x < _drawnArea[2] && x + width > _drawnArea[0] && y < _drawnArea[3] && y + height > _drawnArea[1];
}

void MagnifierLinux::handleDamage(const XEvent& ev) {
    const auto& de = reinterpret_cast<const XDamageNotifyEvent&>(ev);
    // Zera o dano acumulado para receber o próximo aviso
    XDamageSubtract(_display, de.damage, None, None);

    if (de.drawable == _background.pixmap || de.damage == _background.damage) {
        if (viewIntersects(de.area.x, de.area.y, de.area.width, de.area.height)) _sceneDirty = true;
        return;
    }
    auto it = _windows.find(de.drawable);
    if (it == _windows.end()) {
        _sceneDirty = true; // não deveria acontecer; na dúvida, redesenha
        return;
    }
    const TrackedWindow& tw = it->second;
    // area é relativa ao canto da janela (sem a borda)
    if (viewIntersects(tw.x + tw.border + de.area.x, tw.y + tw.border + de.area.y, de.area.width,
                       de.area.height)) {
        _sceneDirty = true;
    }
}

void MagnifierLinux::releaseBackground() {
    forgetDamage(_background, true);
    _background.pixmap = 0; // não é nosso: não chama XFreePixmap
    releaseWindowPixmap(_background);
    _background.viewable = false;
}

// Papel de parede definido na raiz. Sem uma janela de área de trabalho
// (xfdesktop, nautilus-desktop...) é só isso que o X mostra atrás das janelas,
// e o pixmap da raiz não é redirecionado pelo Composite.
void MagnifierLinux::refreshBackground() {
    if (!_backgroundDirty) return;
    _backgroundDirty = false;
    releaseBackground();

    Pixmap pixmap = 0;
    for (Atom atom : {_atomRootPmap, _atomEsetroot}) {
        Atom type = 0;
        int format = 0;
        unsigned long count = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(_display, _root, atom, 0, 1, False, XA_PIXMAP, &type, &format, &count, &after,
                               &data) == Success && data) {
            if (type == XA_PIXMAP && format == 32 && count == 1) pixmap = *reinterpret_cast<Pixmap*>(data);
            XFree(data);
        }
        if (pixmap) break;
    }
    if (!pixmap) return;

    // O pixmap pode já ter sido liberado por quem o criou. XSync antes, para
    // que erros pendentes de outras requisições não sejam contados aqui.
    XSync(_display, False);
    unsigned long errorsBefore = s_xErrorCount.load();
    Window rootRet = 0;
    int x = 0, y = 0;
    unsigned int w = 0, h = 0, border = 0, depth = 0;
    Status ok = XGetGeometry(_display, pixmap, &rootRet, &x, &y, &w, &h, &border, &depth);
    if (!ok || s_xErrorCount.load() != errorsBefore || w == 0 || h == 0) return;

    const PixmapFormat& fmt = pixmapFormatForDepth(static_cast<int>(depth));
    if (!fmt.valid) return;
    const int pixAttribs[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, fmt.rgba ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
        None
    };
    GLXPixmap glxPixmap = glXCreatePixmap(_display, fmt.config, pixmap, pixAttribs);
    XSync(_display, False);
    if (s_xErrorCount.load() != errorsBefore || !glxPixmap) return;

    _background.glxPixmap = glxPixmap;
    _background.width = static_cast<int>(w);
    _background.height = static_cast<int>(h);
    _background.depth = static_cast<int>(depth);
    _background.yInverted = fmt.yInverted;
    _background.opaque = true; // fundo não tem transparência, mesmo em 32 bits
    _background.viewable = true;
    trackDamage(pixmap, _background); // xfdesktop, por exemplo, redesenha no mesmo pixmap
    glGenTextures(1, &_background.texture);
    glBindTexture(GL_TEXTURE_2D, _background.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::cout << "[MagnifierLinux] Papel de parede: pixmap 0x" << std::hex << pixmap << std::dec << " " << w
              << "x" << h << "\n";
}

void MagnifierLinux::refreshWindowList() {
    if (!_stackDirty) return;
    _stackDirty = false;

    Window rootRet = 0, parentRet = 0;
    Window* children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(_display, _root, &rootRet, &parentRet, &children, &n)) return;

    _stack.assign(children, children + n);
    if (children) XFree(children);

    // Remove janelas que deixaram de ser filhas da raiz
    for (auto it = _windows.begin(); it != _windows.end();) {
        if (std::find(_stack.begin(), _stack.end(), it->first) == _stack.end()) {
            forgetDamage(it->second, true);
            releaseWindowPixmap(it->second);
            it = _windows.erase(it);
        } else {
            ++it;
        }
    }
}

void MagnifierLinux::refreshWindow(Window w, TrackedWindow& tw) {
    if (tw.attrsDirty) {
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(_display, w, &attrs)) {
            tw.viewable = false;
            return;
        }
        tw.attrsDirty = false;
        tw.x = attrs.x;
        tw.y = attrs.y;
        tw.width = attrs.width;
        tw.height = attrs.height;
        tw.border = attrs.border_width;
        tw.depth = attrs.depth;
        tw.inputOnly = (attrs.c_class == InputOnly);
        tw.viewable = (attrs.map_state == IsViewable);
        tw.pixmapDirty = true;
        if (!tw.inputOnly) trackDamage(w, tw);
        // Muda quando o gerenciador de janelas coloca ou tira a moldura
        tw.client = findClient(w, 0);
        tw.hiddenDirty = true;
        if (tw.client) XSelectInput(_display, tw.client, PropertyChangeMask);
    }

    if (!tw.viewable || tw.inputOnly || !tw.pixmapDirty) return;

    releaseWindowPixmap(tw);
    const PixmapFormat& fmt = pixmapFormatForDepth(tw.depth);
    if (!fmt.valid) return;

    // Erros aqui são esperados quando a janela some no meio do caminho
    unsigned long errorsBefore = s_xErrorCount.load();
    Pixmap pixmap = XCompositeNameWindowPixmap(_display, w);
    XSync(_display, False);
    if (s_xErrorCount.load() != errorsBefore || !pixmap) {
        tw.viewable = false;
        tw.attrsDirty = true;
        return;
    }

    const int pixAttribs[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, fmt.rgba ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
        None
    };
    GLXPixmap glxPixmap = glXCreatePixmap(_display, fmt.config, pixmap, pixAttribs);
    XSync(_display, False);
    if (s_xErrorCount.load() != errorsBefore || !glxPixmap) {
        XFreePixmap(_display, pixmap);
        tw.viewable = false;
        tw.attrsDirty = true;
        return;
    }

    tw.pixmap = pixmap;
    tw.glxPixmap = glxPixmap;
    tw.yInverted = fmt.yInverted;
    tw.opaque = (tw.depth != 32);
    glGenTextures(1, &tw.texture);
    glBindTexture(GL_TEXTURE_2D, tw.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    tw.pixmapDirty = false;
}

void MagnifierLinux::updateCursorImage() {
    if (!_hasFixes || !_cursorDirty) return;
    _cursorDirty = false;

    XFixesCursorImage* img = XFixesGetCursorImage(_display);
    if (!img) return;

    // Os pixels vêm como unsigned long (64 bits em x86_64) com ARGB nos 32 bits baixos
    std::vector<uint32_t> pixels(static_cast<size_t>(img->width) * img->height);
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<uint32_t>(img->pixels[i]);
    }
    _cursorWidth = img->width;
    _cursorHeight = img->height;
    _cursorHotX = img->xhot;
    _cursorHotY = img->yhot;
    XFree(img);

    glBindTexture(GL_TEXTURE_2D, _cursorTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _cursorWidth, _cursorHeight, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
}

void MagnifierLinux::setVisible(bool visible) {
    if (visible == _visible) return;
    _visible = visible;
    if (visible) {
        XMapRaised(_display, _overlayWindow);
        // O cursor real fica escondido; desenhamos a versão ampliada na cena
        if (_hasFixes && !_cursorHidden) {
            XFixesHideCursor(_display, _root);
            _cursorHidden = true;
        }
    } else {
        XUnmapWindow(_display, _overlayWindow);
        if (_hasFixes && _cursorHidden) {
            XFixesShowCursor(_display, _root);
            _cursorHidden = false;
        }
    }
    XFlush(_display);
}

// --------------------------------------------------------------------------
// Renderização
// --------------------------------------------------------------------------

void MagnifierLinux::render() {
    if (!_display || !_overlayWindow || _glFailed) return;

    float zoom, viewX, viewY;
    std::array<float, 25> colour;
    ReadingOverlay reading;
    {
        std::lock_guard<std::mutex> lock(_stateMutex);
        zoom = _zoomFactor;
        viewX = _viewX;
        viewY = _viewY;
        colour = _colourArray;
        reading = _reading;
    }

    if (!_glReady) {
        if (!_hasComposite || !initGL()) {
            _glFailed = true;
            std::cerr << "[MagnifierLinux] Renderização desativada.\n";
            return;
        }
        _glReady = true;
    }

    processEvents();

    // Igual ao original: a lupa só aparece se houver ampliação, cor ou leitura
    bool colourIsIdentity = true;
    static const int identity[] = {0, 6, 12};
    for (int idx : identity) colourIsIdentity &= std::abs(colour[idx] - 1.0f) < 0.005f;
    for (int idx : {1, 2, 5, 7, 10, 11, 20, 21, 22}) colourIsIdentity &= std::abs(colour[idx]) < 0.005f;
    bool needed = zoom > 1.001f || !colourIsIdentity || reading.visible;

    if (needed && !_visible) _sceneDirty = true;
    setVisible(needed);
    if (!_visible) return;

    if (zoom < 1.0f) zoom = 1.0f;
    float viewW = _width / zoom;
    float viewH = _height / zoom;
    Point2D pointer = queryPointer();

    // Nada mudou desde o último quadro: o conteúdo da lupa continua na tela.
    // Um quadro por segundo mesmo assim, por segurança (dano não avisado).
    const auto now = std::chrono::steady_clock::now();
    bool changed = _sceneDirty || zoom != _drawnZoom || viewX != _drawnViewX || viewY != _drawnViewY ||
                   colour != _drawnColour || !sameReading(reading, _drawnReading) ||
                   !(pointer == _drawnPointer) || now - _lastDraw > std::chrono::seconds(1);
    if (!changed) return;

    _sceneDirty = false;
    _drawnZoom = zoom;
    _drawnViewX = viewX;
    _drawnViewY = viewY;
    _drawnColour = colour;
    _drawnReading = reading;
    _drawnPointer = pointer;
    _lastDraw = now;

    drawScene(viewX, viewY, viewW, viewH, reading, pointer);
    drawMagnified(zoom, viewX, viewY, colour);
    glXSwapBuffers(_display, _glxWindow);
}

void MagnifierLinux::drawScene(float viewX, float viewY, float viewW, float viewH, const ReadingOverlay& reading,
                               Point2D pointer) {
    refreshWindowList();
    updateCursorImage();

    glBindFramebuffer(GL_FRAMEBUFFER, _sceneFBO);
    glViewport(0, 0, _width, _height);

    // Só a área visível da cena precisa ser desenhada
    int sx = std::max(0, static_cast<int>(std::floor(viewX)) - 1);
    int sy = std::max(0, static_cast<int>(std::floor(viewY)) - 1);
    int sx2 = std::min(_width, static_cast<int>(std::ceil(viewX + viewW)) + 1);
    int sy2 = std::min(_height, static_cast<int>(std::ceil(viewY + viewH)) + 1);
    if (sx2 <= sx || sy2 <= sy) { sx = 0; sy = 0; sx2 = _width; sy2 = _height; }
    _drawnArea[0] = sx;
    _drawnArea[1] = sy;
    _drawnArea[2] = sx2;
    _drawnArea[3] = sy2;
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, _height - sy2, sx2 - sx, sy2 - sy);

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_windowProgram);
    glUniform2f(_locWinScreen, static_cast<float>(_width), static_cast<float>(_height));
    glUniform1i(_locWinTexture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(_quadVAO);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // pixmaps ARGB do X são pré-multiplicados

    refreshBackground();
    if (_background.viewable) {
        glBindTexture(GL_TEXTURE_2D, _background.texture);
        _bindTexImage(_display, _background.glxPixmap, GLX_FRONT_LEFT_EXT, nullptr);
        glUniform4f(_locWinRect, 0.f, 0.f, static_cast<float>(_background.width),
                    static_cast<float>(_background.height));
        glUniform1i(_locWinFlipY, _background.yInverted ? 0 : 1);
        glUniform1i(_locWinOpaque, 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        _releaseTexImage(_display, _background.glxPixmap, GLX_FRONT_LEFT_EXT);
    }

    for (Window w : _stack) {
        if (w == _overlayWindow) continue;
        TrackedWindow& tw = _windows[w];
        refreshWindow(w, tw);
        if (!tw.viewable || !tw.glxPixmap || isHidden(tw)) continue;

        float fw = static_cast<float>(tw.width + 2 * tw.border);
        float fh = static_cast<float>(tw.height + 2 * tw.border);
        if (tw.x + fw < sx || tw.x > sx2 || tw.y + fh < sy || tw.y > sy2) continue;

        glBindTexture(GL_TEXTURE_2D, tw.texture);
        _bindTexImage(_display, tw.glxPixmap, GLX_FRONT_LEFT_EXT, nullptr);
        glUniform4f(_locWinRect, static_cast<float>(tw.x), static_cast<float>(tw.y), fw, fh);
        glUniform1i(_locWinFlipY, tw.yInverted ? 0 : 1);
        glUniform1i(_locWinOpaque, tw.opaque ? 1 : 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        _releaseTexImage(_display, tw.glxPixmap, GLX_FRONT_LEFT_EXT);
    }

    // A janela Reading do original é TopMost: fica acima das janelas e é ampliada junto
    if (reading.visible && reading.opacity > 0.0f) {
        drawReadingOverlay(reading);
        glUseProgram(_windowProgram);
        glBindVertexArray(_quadVAO);
    }

    // Cursor desenhado na cena para ser ampliado junto, como no Windows
    if (_cursorWidth > 0 && _cursorHeight > 0) {
        const Point2D& p = pointer;
        glBindTexture(GL_TEXTURE_2D, _cursorTexture);
        glUniform4f(_locWinRect, static_cast<float>(p.x - _cursorHotX), static_cast<float>(p.y - _cursorHotY),
                    static_cast<float>(_cursorWidth), static_cast<float>(_cursorHeight));
        glUniform1i(_locWinFlipY, 0);
        glUniform1i(_locWinOpaque, 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MagnifierLinux::drawReadingOverlay(const ReadingOverlay& r) {
    glUseProgram(_readingProgram);
    glUniform2f(_locReadScreen, static_cast<float>(_width), static_cast<float>(_height));
    glUniform1f(_locReadOpacity, r.opacity);
    glUniform3fv(_locReadBack, 1, r.back);
    const bool wholeScreen = r.areaWidth <= 0 || r.areaHeight <= 0;
    glUniform4f(_locReadArea, wholeScreen ? 0.0f : static_cast<float>(r.areaX), wholeScreen ? 0.0f : static_cast<float>(r.areaY),
                wholeScreen ? static_cast<float>(_width) : static_cast<float>(r.areaWidth),
                wholeScreen ? static_cast<float>(_height) : static_cast<float>(r.areaHeight));
    glUniform4f(_locReadBand, static_cast<float>(r.bandX), static_cast<float>(r.bandY),
                static_cast<float>(r.bandWidth), static_cast<float>(r.bandHeight));
    glUniform1i(_locReadLines, r.linesVisible ? 1 : 0);
    glUniform1f(_locReadLinesBase, r.linesBaseY);
    glUniform1f(_locReadLineHeight, static_cast<float>(r.lineHeight));
    glUniform1f(_locReadLineOpacity, static_cast<float>(r.lineOpacity));
    glUniform3fv(_locReadLineColour, 1, r.lineColour);
    glUniform1i(_locReadDouble, r.doubleLines ? 1 : 0);
    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void MagnifierLinux::drawMagnified(float zoom, float viewX, float viewY, const std::array<float, 25>& colour) {
    glViewport(0, 0, _width, _height);
    glUseProgram(_magnifyProgram);

    // Matriz 5x5 (linha) -> mat3 GLSL (coluna): as linhas do Windows viram colunas
    const float colMat[9] = {
        colour[0], colour[1], colour[2],
        colour[5], colour[6], colour[7],
        colour[10], colour[11], colour[12]
    };
    const float colOff[3] = { colour[20], colour[21], colour[22] };

    glUniform2f(_locMagScreen, static_cast<float>(_width), static_cast<float>(_height));
    glUniform2f(_locMagView, viewX, viewY);
    glUniform1f(_locMagZoom, zoom);
    glUniform1i(_locMagTexture, 0);
    glUniformMatrix3fv(_locMagColorMatrix, 1, GL_FALSE, colMat);
    glUniform3fv(_locMagColorOffset, 1, colOff);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _sceneTexture);
    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace LupaLinux
