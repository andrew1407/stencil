// Windows body of shareImage.hpp — DataTransferManager (WinRT), the same "Mail /
// Nearby Share / any installed share target" flyout the Photos app's own Share
// button shows. C++/WinRT projection headers ship with the Windows SDK (no extra
// package — desktop/CMakeLists.txt links `windowsapp` for them), so this stays
// inside the no-new-dependencies rule the same way AppKit does on macOS.
//
// UNVERIFIED: written from the documented Win32-desktop-app share-source pattern
// (IDataTransferManagerInterop bridging a plain HWND into the WinRT DataTransferManager)
// but this repo has no Windows toolchain to actually compile or run it against — a
// Windows build/CI pass owes this file a first real compile.
#include "shareImage.hpp"

#include <QWidget>

#include <map>

// IDataTransferManagerInterop is declared by shobjidl_core.h (there is no separate
// interop header for it in the SDK). NOMINMAX keeps windows.h's min/max macros off
// the WinRT headers that follow.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shobjidl_core.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

namespace stencil::support {

  namespace {
    using namespace winrt::Windows::ApplicationModel::DataTransfer;
    using namespace winrt::Windows::Storage;
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Windows::Foundation::Collections;

    // The request has to stay answerable until the async file lookup below resolves,
    // hence the deferral; a plain synchronous SetStorageItems here would hand the
    // share target an empty package on any machine where the disk read isn't instant.
    // A Completed handler rather than co_await: the app is C++17 without /await, and
    // the projection's coroutine support needs one or the other.
    void fulfil(DataRequest request, QString filePath, QString title) {
      auto deferral = request.GetDeferral();
      const auto props = request.Data().Properties();
      props.Title(winrt::hstring(title.toStdWString()));
      StorageFile::GetFileFromPathAsync(winrt::hstring(filePath.toStdWString()))
          .Completed([request, deferral](IAsyncOperation<StorageFile> const& op, AsyncStatus) {
            try {
              auto items = winrt::single_threaded_vector<IStorageItem>();
              items.Append(op.GetResults());
              request.Data().SetStorageItems(items);
            } catch (winrt::hresult_error const&) {
              request.FailWithDisplayText(L"Could not prepare the file to share");
            }
            deferral.Complete();
          });
    }
  }  // namespace

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    if (!anchor) return false;
    const HWND hwnd = reinterpret_cast<HWND>(anchor->winId());
    if (!hwnd) return false;

    // A plain HWND has no DataTransferManager of its own — that's a UWP-window
    // concept. IDataTransferManagerInterop is the documented bridge: it hands back
    // the manager FOR this window, and later shows the share UI anchored to it.
    auto interop = winrt::get_activation_factory<DataTransferManager, IDataTransferManagerInterop>();
    DataTransferManager manager{nullptr};
    if (FAILED(interop->GetForWindow(hwnd, winrt::guid_of<DataTransferManager>(),
                                     winrt::put_abi(manager))))
      return false;

    // GetForWindow hands back the SAME per-HWND manager every call, so a handler left
    // registered would accumulate (N shares = N handlers). Revoke this window's
    // previous registration before adding the one for this share.
    static std::map<HWND, winrt::event_token> tokens;
    if (auto it = tokens.find(hwnd); it != tokens.end()) manager.DataRequested(it->second);
    tokens[hwnd] = manager.DataRequested(
        [filePath, title](DataTransferManager const&, DataRequestedEventArgs const& e) {
          fulfil(e.Request(), filePath, title);
        });

    if (FAILED(interop->ShowShareUIForWindow(hwnd))) return false;
    return true;
  }

}  // namespace stencil::support
