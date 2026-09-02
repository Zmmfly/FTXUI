// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_COMPONENT_APP_HPP
#define FTXUI_COMPONENT_APP_HPP

#include <atomic>      // for atomic
#include <chrono>      // for steady_clock, time_point
#include <functional>  // for function
#include <memory>      // for shared_ptr, unique_ptr
#include <string>      // for string, basic_string, allocator
#include <vector>      // for vector

#include "ftxui/component/animation.hpp"  // for TimePoint
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/task.hpp"   // for Task, Closure
#include "ftxui/screen/screen.hpp"    // for Screen
#include "ftxui/screen/terminal.hpp"  // for Dimensions
#include "ftxui/util/export.hpp"

namespace ftxui {
class ComponentBase;
using Component = std::shared_ptr<ComponentBase>;
struct Event;
class Selection;
class TaskRunner;

/// @brief Optional per-screen callbacks for latency instrumentation.
///
/// Callbacks may run on any thread posting a task or on the screen loop thread
/// drawing a frame. They must remain non-blocking. Exceptions are ignored.
struct ScreenInteractivePerformanceObserver {
  /// Called after a public Post/TryPost operation has actually queued a task.
  std::function<void()> on_public_post_accepted;

  /// Called after a rendered Draw completes, or for a cached Draw skip.
  ///
  /// A cached skip reports zero elapsed time and `rendered == false`.
  std::function<void(std::chrono::nanoseconds elapsed, bool rendered)>
      on_draw;
};

/// @brief App is a class that manages the application lifecycle.
/// It is responsible for initializing the terminal, running the main loop,
/// and cleaning up on exit.
///
/// @note This class was previously named ScreenInteractive.
///
/// @ingroup component
class FTXUI_EXPORT(COMPONENT) App : public Screen {
 public:
  // Constructors:

  /// @brief Create an App with a fixed size.
  /// @param dimx The width of the app.
  /// @param dimy The height of the app.
  static App FixedSize(int dimx, int dimy);

  /// @brief Create an App taking the full terminal size. This is using the
  /// alternate screen buffer to avoid messing with the terminal content.
  /// @note This is the same as `App::FullscreenAlternateScreen()`
  static App Fullscreen();

  /// @brief Create an App taking the full terminal size. The primary screen
  /// buffer is being used. It means if the terminal is resized, the previous
  /// content might mess up with the terminal content.
  static App FullscreenPrimaryScreen();

  /// @brief Create an App taking the full terminal size. This is using the
  /// alternate screen buffer to avoid messing with the terminal content.
  static App FullscreenAlternateScreen();

  /// @brief Create an App whose width and height match the component being
  /// drawn.
  static App FitComponent();

  /// @brief Create an App whose width match the terminal output width and
  /// the height matches the component being drawn.
  static App TerminalOutput();

  // Destructor.
  ~App() override;

  App(App&&) noexcept;
  App& operator=(App&&) noexcept;
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Options. Must be called before Loop().

  /// @brief Set whether mouse is tracked and events reported.
  /// @param enable Whether to enable mouse event tracking.
  /// @note Mouse tracking is enabled by default.
  /// @note Mouse tracking is only supported on terminals that supports it.
  /// @note This must be called before calling `App::Loop`.
  void TrackMouse(bool enable = true);

  /// @brief Enable or disable automatic piped input handling.
  /// When enabled, FTXUI will detect piped input and redirect stdin from
  /// /dev/tty for keyboard input, allowing applications to read piped data
  /// while still receiving interactive keyboard events.
  /// @param enable Whether to enable piped input handling. Default is true.
  /// @note This must be called before Loop().
  /// @note This feature is enabled by default.
  /// @note This feature is only available on POSIX systems (Linux/macOS).
  void HandlePipedInput(bool enable = true);

  /// @brief Return the currently active app, nullptr if none.
  static App* Active();

  /// @brief Best-effort terminal restoration for abnormal termination.
  ///
  /// Uses only pre-captured terminal state and fixed control sequences, is
  /// safe to call repeatedly, and is also driven by FTXUI's fatal-signal
  /// handlers, including the path used by std::terminate.
  static void EmergencyRestoreTerminal() noexcept;

  // Start/Stop the main loop.

  /// @brief Execute the main loop.
  /// @param component The component to draw.
  void Loop(Component component);

  /// @brief Exit the main loop.
  void Exit();

  /// @brief Return a function to exit the main loop.
  Closure ExitLoopClosure();

  /// @brief Decorate a function. The outputted one will execute similarly to
  /// the inputted one, but with the currently active app terminal hooks
  /// temporarily uninstalled.
  Closure WithRestoredIO(Closure fn);

  /// @brief FTXUI implements handlers for Ctrl-C and Ctrl-Z. By default, these
  /// handlers are executed, even if the component catches the event. This avoid
  /// users handling every event to be trapped in the application. However, in
  /// some cases, the application may want to handle these events itself. In
  /// this case, the application can force FTXUI to not handle these events by
  /// calling the following functions with force=true.
  void ForceHandleCtrlC(bool force = true);

  /// @brief Force FTXUI to handle or not handle Ctrl-Z, even if the component
  /// catches the Event::CtrlZ.
  void ForceHandleCtrlZ(bool force = true);

  // Post tasks to be executed by the loop.

  /// @brief Try to add a task to the main loop.
  /// @param task Task to enqueue.
  /// @return True only when the screen was active and accepted the task.
  bool TryPost(Task task);

  /// @brief Try to add an event to the main loop.
  /// @param event Event to enqueue.
  /// @return True only when the screen was active and accepted the event.
  bool TryPostEvent(Event event);

  /// @brief Add a task to the main loop.
  /// It will be executed later, after every other scheduled tasks.
  void Post(Task task);

  /// @brief Add an event to the main loop.
  /// It will be executed later, after every other scheduled events.
  void PostEvent(Event event);

  /// @brief Replace the optional per-screen performance observer.
  /// @param observer Callbacks copied for concurrent notification.
  ///
  /// Callback invocation never holds the observer mutex.
  void SetPerformanceObserver(
      ScreenInteractivePerformanceObserver observer);

  /// @brief Add a task to the main loop.
  /// It will be executed later, after every other scheduled tasks.
  static void PostEventOrExecute(Closure closure);

  /// @brief Add a task to draw the screen one more time, until all the
  /// animations are done.
  void RequestAnimationFrame();

  // Selection API:

  /// @brief Try to get the unique lock about being able to capture the mouse.
  /// @return A unique lock if the mouse is not already captured, otherwise a
  /// null.
  CapturedMouse CaptureMouse();

  /// @brief Returns the content of the current selection.
  std::string GetSelection();

  /// @brief Set a callback that will be called when the selection changes.
  void SelectionChange(std::function<void()> callback);

  /// @brief Set a callback invoked once, after the frame that finalizes a
  /// drag selection. The finalized text is readable via GetSelection().
  void SelectionEnd(std::function<void()> callback);

  /// @brief Set a handler consulted on pointer motion while a drag selection
  /// is active. It may scroll its viewport and call ShiftSelection to keep
  /// the highlight glued to the scrolled content.
  /// @param handler Receives the translated pointer cell coordinates.
  void SelectionAutoScroll(std::function<void(int, int)> handler);

  /// @brief Translate the active selection anchors by the given screen cells.
  /// Used after scrolling so the selection follows the scrolled content.
  void ShiftSelection(int dx, int dy);

  // Terminal info.

  /// @brief Return the terminal name.
  const std::string& TerminalName() const;

  /// @brief Return the terminal version.
  int TerminalVersion() const;

  /// @brief Return the terminal emulator name.
  const std::string& TerminalEmulatorName() const;

  /// @brief Return the terminal emulator version.
  const std::string& TerminalEmulatorVersion() const;

  /// @brief Return the terminal capabilities.
  const std::vector<int>& TerminalCapabilities() const;

  /// @brief Return the names of the terminal capabilities.
  std::vector<std::string> TerminalCapabilityNames() const;

 private:
  void ExitNow();
  void Install();
  void Uninstall();

  void PreMain();
  void PostMain();

  /// @brief Return whether the main loop has been quit.
  bool HasQuitted();
  void RunOnce(const Component& component);
  void RunOnceBlocking(Component component);

  void HandleTask(Component component, Task& task);
  bool HandleSelection(bool handled, Event event);
  void Draw(Component component);
  std::string ResetCursorPosition();

  void RequestCursorPosition(bool force = false);

  void TerminalSend(std::string_view);
  void TerminalFlush();

  void InstallPipedInputHandling();
  void InstallTerminalInfo();

  void Signal(int signal);

  size_t FetchTerminalEvents();

  void PostAnimationTask();

  struct Internal;
  explicit App(std::unique_ptr<Internal> internal, int dimx, int dimy);

  std::unique_ptr<Internal> internal_;

  friend class Loop;

 public:
  class FTXUI_EXPORT(COMPONENT) Private {
   public:
    static void Signal(App& s, int signal) { s.Signal(signal); }

    /// Inject a parser-produced event into the next terminal-input batch.
    /// This is an internal test seam; ordinary callers should use PostEvent.
    static void InjectTerminalEventForTesting(App& app, Event event);

    /// Maximum platform input units drained by one RunOnce frame.
    static size_t TerminalInputDrainBudgetForTesting();
  };
  friend Private;
};

}  // namespace ftxui

#endif /* end of include guard: FTXUI_COMPONENT_APP_HPP */
