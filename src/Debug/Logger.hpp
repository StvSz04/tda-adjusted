#pragma once

#include "../Utils/PrintUtils.hpp"

#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include <list>
#include <string>

namespace tda {

// Trait to detect iterable types (has begin() and end())
template <typename T, typename = void>
struct is_iterable : std::false_type {};

template <typename T>
struct is_iterable<T, std::void_t<decltype(std::begin(std::declval<T>())), decltype(std::end(std::declval<T>()))>>
: std::true_type {};

template <typename T>
constexpr bool is_iterable_v = is_iterable<T>::value;

// Concept for Printable objects
template <typename T>
concept PrintableConcept = is_printable<T>::value;

// Concept for smart pointer
template <typename P>
concept SmartPtrLike = requires(const P& p) {
  { p.get() };                                   // has .get()
  requires std::is_pointer_v<decltype(p.get())>; // and .get() returns an actual pointer type
};

template <typename P>
using smart_pointee_t = std::remove_pointer_t<decltype(std::declval<const P&>().get())>;

// Concept for smart pointer to Printable objects
template <typename P>
concept SmartPtrToPrintable = SmartPtrLike<P> && PrintableConcept<smart_pointee_t<P>>;

// Concept for LLVMPrintable objects
template <typename T>
concept LLVMPrintableConcept = has_llvm_print_v<T> && !PrintableConcept<T>;

// Concept for iterable objects (exclude std::string and llvm::StringRef from being treated as an iterable container)
template <typename T>
concept IterableConcept = is_iterable_v<T> && !std::is_same_v<std::decay_t<T>, std::string>
                       && !std::is_same_v<std::decay_t<T>, llvm::StringRef>;

class Logger {
public:
  class Indenter {
    friend class Logger;

  public:
    Logger& increaseIndent(unsigned amount = 1) {
      logger.increaseIndent(amount);
      indent += amount;
      return logger;
    }

    Logger& decreaseIndent(unsigned amount = 1) {
      if (indent >= amount) {
        logger.decreaseIndent(amount);
        indent -= amount;
      }
      else {
        logger.decreaseIndent(indent);
        indent = 0;
      }
      return logger;
    }

    ~Indenter() { logger.decreaseIndent(indent); }

  private:
    Logger& logger;
    unsigned indent;

    Indenter(Logger& logger)
    : logger(logger), indent(0) {}
  };

  enum Color {
    Current = -3,
    Reset = -2,
    Bold = -1,
    Black = static_cast<int>(llvm::raw_ostream::Colors::BLACK),
    Red = static_cast<int>(llvm::raw_ostream::Colors::RED),
    Green = static_cast<int>(llvm::raw_ostream::Colors::GREEN),
    Yellow = static_cast<int>(llvm::raw_ostream::Colors::YELLOW),
    Blue = static_cast<int>(llvm::raw_ostream::Colors::BLUE),
    Magenta = static_cast<int>(llvm::raw_ostream::Colors::MAGENTA),
    Cyan = static_cast<int>(llvm::raw_ostream::Colors::CYAN),
    White = static_cast<int>(llvm::raw_ostream::Colors::WHITE),
  };

  static Logger& getInstance();

  Logger& logValue(const llvm::Value* value, bool logParent = true);
  Logger& logValueln(const llvm::Value* value, bool logParent = true);

  Logger& setContextTag(const std::string& tag);
  Logger& setContextTag(const char* tag);
  Logger& restorePrevContextTag();

  Logger& setColor(Color color);
  Logger& resetColor();

  [[nodiscard]] Indenter getIndenter() { return Indenter(*this); }

  // Log for Printable objects
  template <PrintableConcept T>
  Logger& log(const T& printable, Color color = Current) {
    log(printable.toString(), color);
    return *this;
  }

  // Log for smart ptr to Printable objects
  template <SmartPtrToPrintable T>
  Logger& log(const T& ptr, Color color = Current) {
    if (auto* rawPtr = ptr.get())
      return log(*rawPtr, color);
    return log("null", color);
  }

  // Log for LLVM-printable objects
  template <LLVMPrintableConcept T>
  Logger& log(const T& printableLLVM, Color color = Current) {
    log(toString(printableLLVM), color);
    return *this;
  }

  // Log for iterable objects
  template <IterableConcept T>
    requires(!PrintableConcept<T> && !LLVMPrintableConcept<T>)
  Logger& log(const T& iterable, Color color = Current) {
    log("[", Bold);
    bool first = true;
    for (const auto& el : iterable) {
      if (!first)
        log(", ", Bold);
      else
        first = false;
      log(el, color);
    }
    log("]", Bold);
    return *this;
  }

  // Generic fallback log (for types that are not printable in any special way)
  template <typename T>
    requires(!PrintableConcept<T> && !SmartPtrToPrintable<T> && !LLVMPrintableConcept<T> && !IterableConcept<T>)
  Logger& log(const T& message, Color color = Current) {
    // Convert message to string
    std::string s;
    llvm::raw_string_ostream oss(s);
    oss << message;
    std::string messageString = oss.str();

    // TODO add conditional ostream.is_displayed() (command line)
    bool useColors = ostream.is_displayed();

    Color resColor;
    bool bold = currentlyBold;
    if (color == Current)
      resColor = currentColor;
    else if (color == Bold) {
      bold = true;
      resColor = currentColor;
    }
    else
      resColor = color;

    if (resColor == Reset) {
      if (useColors)
        ostream.resetColor();
      if (!bold)
        useColors = false;
    }
    else
      bold = true;

    // Log the string splitting by '\n'.
    size_t start = 0;
    while (start < messageString.size()) {
      size_t pos = messageString.find('\n', start);
      std::string line =
        (pos == std::string::npos) ? messageString.substr(start) : messageString.substr(start, pos - start);
      if (isLineStart)
        logIndent();
      if (useColors) {
        auto finalColor = (resColor == Reset) ? llvm::raw_ostream::Colors::SAVEDCOLOR
                                              : static_cast<llvm::raw_fd_ostream::Colors>(resColor);
        ostream.changeColor(finalColor, bold);
      }
      ostream << line;
      if (useColors)
        ostream.resetColor();
      // If no newline was found, stop.
      if (pos == std::string::npos) {
        isLineStart = false;
        break;
      }
      ostream << "\n";
      isLineStart = true;
      start = pos + 1;
    }
    ostream.flush();
    return *this;
  }

  Logger& log(const bool boolean, Color color = Current) {
    log(boolean ? "true" : "false", color);
    return *this;
  }

  template <typename T>
  Logger& logln(const T& value, Color color = Current) {
    log(value, color);
    ostream << "\n";
    isLineStart = true;
    ostream.flush();
    return *this;
  }

  template <typename T>
  Logger& operator<<(const T& value) {
    log(value);
    return *this;
  }

  Logger& operator<<(Color color) {
    setColor(color);
    return *this;
  }

private:
  llvm::raw_ostream& ostream;
  std::list<std::string> contextTagStack;
  Color currentColor = Reset;
  bool currentlyBold = false;
  unsigned indent = 0;
  bool isLineStart = true;

  Logger()
  : ostream(getOutputStream()) {}

  Logger(const Logger&) = delete;

  Logger& operator=(const Logger&) = delete;

  static llvm::raw_fd_ostream& getOutputStream();
  void logIndent();

  Logger& increaseIndent(unsigned amount = 1);
  Logger& decreaseIndent(unsigned amount = 1);
};

inline Logger& log() { return Logger::getInstance(); }

} // namespace tda
