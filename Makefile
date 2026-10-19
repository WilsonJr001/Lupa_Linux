CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude -MMD -MP
LDFLAGS ?= -lX11 -lGL -l:libXext.so.6 -l:libXcomposite.so.1 -l:libXfixes.so.3 -l:libXi.so.6 -l:libXrandr.so.2 -l:libXdamage.so.1 -l:libatspi.so.0 -l:libgobject-2.0.so.0 -l:libglib-2.0.so.0 -lpthread

SRCDIR = src
OBJDIR = obj
BINDIR = .

SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
TARGET = $(BINDIR)/glassbrick

.PHONY: all clean run test tools

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build concluída com sucesso: $(TARGET)"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Testes sem servidor X: só os objetos de lógica (libX11 entra apenas para
# converter nomes de teclas)
TEST_OBJECTS = $(OBJDIR)/Camera.o $(OBJDIR)/GlassbrickSettings.o $(OBJDIR)/SettingsFile.o
TEST_BINS = $(OBJDIR)/test_camera $(OBJDIR)/test_settings

$(OBJDIR)/test_%: tests/test_%.cpp $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $< $(TEST_OBJECTS) -o $@ -lX11 -lpthread

test: $(TEST_BINS) $(TARGET)
	@for t in $(TEST_BINS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "== tests/test_ui_config.py"; python3 tests/test_ui_config.py ./$(TARGET)

# Ferramentas para testes na tela (screenshot, pixel, teclas simuladas, atalhos)
TOOLS = $(OBJDIR)/tools/shot $(OBJDIR)/tools/pix $(OBJDIR)/tools/grabcheck $(OBJDIR)/tools/keys $(OBJDIR)/tools/setbg $(OBJDIR)/tools/blink
tools: $(TOOLS)

$(OBJDIR)/tools/keys: tests/tools/keys.c
	@mkdir -p $(OBJDIR)/tools
	$(CC) -O2 $< -o $@ -lX11 -l:libXtst.so.6

$(OBJDIR)/tools/%: tests/tools/%.c
	@mkdir -p $(OBJDIR)/tools
	$(CC) -O2 $< -o $@ -lX11

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(OBJDIR) $(TARGET)
	@echo "Limpeza concluída."

run: $(TARGET)
	./$(TARGET)
