#pragma once

#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <functional>
#include <algorithm>
#include <map>

namespace Glassbrick {

// Modos de rastreamento da câmera
enum class TrackMode {
    Centre = 1,
    Consistent,
    Border,
    KeyCombo,
    ScreenLocation,
    NoTracking
};

// Modos de cores e acessibilidade
enum class ColourMode {
    Default = 1,
    Invert,
    Adjust,
    Tint,
    Contrast
};

// Modos de simulação e correção para daltonismo
enum class AdjustMode {
    Deuteranomaly, // Verde fraco / ausente
    Protanomaly,   // Vermelho fraco / ausente
    Tritanomaly,   // Azul fraco / ausente
    Monochromacy   // Escala de cinza / acromatopsia
};

// Modos de alto contraste
enum class ContrastMode {
    FullColour,
    BlackAndWhite,
    BlackAndYellow,
    BlueAndYellow,
    BlueAndWhite
};

// Tipos de inversão
enum class InvertMode {
    Brightness,
    Colour
};

// Paleta de matizes para coloração (Tint)
enum class TintColour {
    Orange,
    Yellow,
    Lime,
    Green,
    Emerald,
    Cyan,
    Azure,
    Blue,
    Purple,
    Magenta,
    Rose,
    Red,
    Black
};

// Modos de renderização
enum class RenderMode {
    Fullscreen,
    Windowed,
    Win8Fullscreen
};

// Ações disparadas por atalhos e teclado/mouse
enum class InputAction {
    ZoomIn,
    ZoomOut,
    ZoomOnToggle,
    ZoomSpeedCycle,
    ZoomAmountCycle,
    TrackModeCycle,
    ColourModeCycle,
    ReadingModeToggle,
    TrackModifiersDown,
    TrackModifiersUp,
    AnyKeyReleased
};

// Máscara de modificadores (Flags)
enum class Modifiers : unsigned int {
    NoModifier = 0,
    Control = 1,
    Windows = 2, // Tecla Super / Meta
    Alt = 4,
    Shift = 8
};

inline Modifiers operator|(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
}

inline Modifiers operator&(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(static_cast<unsigned int>(a) & static_cast<unsigned int>(b));
}

inline Modifiers& operator|=(Modifiers& a, Modifiers b) {
    a = a | b;
    return a;
}

inline Modifiers& operator&=(Modifiers& a, Modifiers b) {
    a = a & b;
    return a;
}

inline Modifiers operator~(Modifiers a) {
    return static_cast<Modifiers>(~static_cast<unsigned int>(a));
}

// Estruturas geométricas básicas
struct Point2D {
    int x = 0;
    int y = 0;

    Point2D() = default;
    Point2D(int _x, int _y) : x(_x), y(_y) {}

    bool operator==(const Point2D& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Point2D& other) const { return !(*this == other); }
};

struct Point2DF {
    float x = 0.0f;
    float y = 0.0f;

    Point2DF() = default;
    Point2DF(float _x, float _y) : x(_x), y(_y) {}
};

struct Vector2D {
    double x = 0.0;
    double y = 0.0;

    Vector2D() = default;
    Vector2D(double _x, double _y) : x(_x), y(_y) {}

    double length() const {
        return std::sqrt(x * x + y * y);
    }

    void normalize() {
        double len = length();
        if (len > 0.000001) {
            x /= len;
            y /= len;
        } else {
            x = 0.0;
            y = 0.0;
        }
    }

    Vector2D operator+(const Vector2D& o) const { return Vector2D(x + o.x, y + o.y); }
    Vector2D operator-(const Vector2D& o) const { return Vector2D(x - o.x, y - o.y); }
    Vector2D operator*(double scalar) const { return Vector2D(x * scalar, y * scalar); }
    Vector2D& operator+=(const Vector2D& o) { x += o.x; y += o.y; return *this; }

    static Vector2D subtract(const Vector2D& a, const Vector2D& b) {
        return a - b;
    }
};

struct Rect2D {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    Rect2D() = default;
    Rect2D(double _x, double _y, double _w, double _h)
        : x(_x), y(_y), width(_w), height(_h) {}

    double left() const { return x; }
    double top() const { return y; }
    double right() const { return x + width; }
    double bottom() const { return y + height; }

    bool contains(double px, double py) const {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }

    bool operator==(const Rect2D& o) const {
        return std::abs(x - o.x) < 0.001 &&
               std::abs(y - o.y) < 0.001 &&
               std::abs(width - o.width) < 0.001 &&
               std::abs(height - o.height) < 0.001;
    }

    bool operator!=(const Rect2D& o) const { return !(*this == o); }
};

// Matriz de cores e transformação (equivalente ao Matrix.cs do Glassbrick)
class Matrix {
private:
    int _rows = 4;
    int _cols = 3;
    std::vector<float> _m;

public:
    Matrix() : _rows(4), _cols(3), _m(12, 0.0f) {}

    Matrix(int rows, int cols) : _rows(rows), _cols(cols), _m(rows * cols, 0.0f) {}

    Matrix(int rows, int cols, const std::vector<float>& initial)
        : _rows(rows), _cols(cols), _m(initial) {
        if (static_cast<int>(_m.size()) != rows * cols) {
            _m.resize(rows * cols, 0.0f);
        }
    }

    Matrix(int rows, int cols, std::function<float(int, int)> func)
        : _rows(rows), _cols(cols), _m(rows * cols) {
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                _m[i * cols + j] = func(i, j);
            }
        }
    }

    int rows() const { return _rows; }
    int cols() const { return _cols; }

    float& operator()(int r, int c) { return _m[r * _cols + c]; }
    float operator()(int r, int c) const { return _m[r * _cols + c]; }

    Matrix operator+(const Matrix& other) const {
        Matrix result(_rows, _cols);
        for (size_t i = 0; i < _m.size(); ++i) {
            result._m[i] = _m[i] + other._m[i];
        }
        return result;
    }

    Matrix operator-(const Matrix& other) const {
        Matrix result(_rows, _cols);
        for (size_t i = 0; i < _m.size(); ++i) {
            result._m[i] = _m[i] - other._m[i];
        }
        return result;
    }

    Matrix operator*(float scalar) const {
        Matrix result(_rows, _cols);
        for (size_t i = 0; i < _m.size(); ++i) {
            result._m[i] = _m[i] * scalar;
        }
        return result;
    }

    Matrix& operator+=(const Matrix& other) {
        for (size_t i = 0; i < _m.size(); ++i) {
            _m[i] += other._m[i];
        }
        return *this;
    }

    bool isIdentity() const {
        for (int i = 0; i < _rows; ++i) {
            for (int j = 0; j < _cols; ++j) {
                float expected = (i == j) ? 1.0f : 0.0f;
                if (std::abs((*this)(i, j) - expected) > 0.001f) {
                    return false;
                }
            }
        }
        return true;
    }

    // Converte para array de 25 floats (matriz de cor 5x5 no padrão GDI+/DirectX/GLSL)
    std::array<float, 25> toColourArray() const {
        std::array<float, 25> arr{};
        arr[0]  = (*this)(0, 0); arr[1]  = (*this)(0, 1); arr[2]  = (*this)(0, 2); arr[3]  = 0.0f; arr[4]  = 0.0f;
        arr[5]  = (*this)(1, 0); arr[6]  = (*this)(1, 1); arr[7]  = (*this)(1, 2); arr[8]  = 0.0f; arr[9]  = 0.0f;
        arr[10] = (*this)(2, 0); arr[11] = (*this)(2, 1); arr[12] = (*this)(2, 2); arr[13] = 0.0f; arr[14] = 0.0f;
        arr[15] = 0.0f;          arr[16] = 0.0f;          arr[17] = 0.0f;          arr[18] = 1.0f; arr[19] = 0.0f;
        arr[20] = (*this)(3, 0); arr[21] = (*this)(3, 1); arr[22] = (*this)(3, 2); arr[23] = 0.0f; arr[24] = 1.0f;
        return arr;
    }
};

inline Matrix operator*(float scalar, const Matrix& m) {
    return m * scalar;
}

// Ciclador de Enum (equivalente a EnumModeCycler.cs)
template <typename T>
class EnumModeCycler {
private:
    T _mode;
    std::map<T, bool> _enabled;
    std::vector<T> _order;

public:
    EnumModeCycler(T initialMode, const std::vector<T>& allModes)
        : _mode(initialMode), _order(allModes) {
        for (const auto& m : allModes) {
            _enabled[m] = true;
        }
    }

    T getMode() const { return _mode; }
    void setMode(T m) { _mode = m; }

    bool isModeEnabled(T m) const {
        auto it = _enabled.find(m);
        return it != _enabled.end() ? it->second : false;
    }

    void setModeEnabled(T m, bool enabled) {
        _enabled[m] = enabled;
    }

    T cycle() {
        std::vector<T> available;
        for (const auto& m : _order) {
            if (isModeEnabled(m)) {
                available.push_back(m);
            }
        }
        if (available.empty()) return _mode;

        for (size_t i = 0; i < available.size(); ++i) {
            if (static_cast<int>(available[i]) > static_cast<int>(_mode)) {
                _mode = available[i];
                return _mode;
            }
        }
        _mode = available.front();
        return _mode;
    }
};

// Ciclador de Inteiros com Função de Valor (equivalente a IntModeCycler.cs)
template <typename TV>
class IntModeCycler {
private:
    int _mode = 0;
    int _modeCount = 0;
    std::function<TV(int)> _valueFunc;
    std::map<int, bool> _enabled;

public:
    IntModeCycler(int initialMode, int modeCount, std::function<TV(int)> valueFunc)
        : _mode(initialMode), _modeCount(modeCount), _valueFunc(valueFunc) {
        for (int i = 0; i < modeCount; ++i) {
            _enabled[i] = true;
        }
    }

    int getMode() const { return _mode; }
    void setMode(int m) {
        if (m >= 0 && m < _modeCount) {
            _mode = m;
        }
    }

    TV getValue() const {
        return _valueFunc(_mode);
    }

    bool isModeEnabled(int m) const {
        auto it = _enabled.find(m);
        return it != _enabled.end() ? it->second : false;
    }

    void setModeEnabled(int m, bool enabled) {
        _enabled[m] = enabled;
    }

    int cycle() {
        std::vector<int> available;
        for (int i = 0; i < _modeCount; ++i) {
            if (isModeEnabled(i)) {
                available.push_back(i);
            }
        }
        if (available.empty()) return _mode;

        for (int val : available) {
            if (val > _mode) {
                _mode = val;
                return _mode;
            }
        }
        _mode = available.front();
        return _mode;
    }
};

} // namespace Glassbrick
