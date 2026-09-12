// Windows body of shareImage.hpp — DataTransferManager (WinRT); C++/WinRT ships with the
// SDK (desktop/CMakeLists.txt links `windowsapp`). UNVERIFIED: this repo has no Windows
// toolchain, so a Windows build owes this file a first real compile.
#include "shareImage.hpp"

#include <QWidget>

#include <map>

// IDataTransferManagerInterop lives in shobjidl_core.h. NOMINMAX keeps min/max off the WinRT headers.
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

    // Deferral: the request must stay answerable until the async file lookup resolves.
    // A Completed handler, not co_await: C++17 without /await.
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

  bool shareSheetAvailable() { return true; }

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    if (!anchor) return false;
    const HWND hwnd = reinterpret_cast<HWND>(anchor->winId());
    if (!hwnd) return false;

    // A plain HWND has no DataTransferManager; IDataTransferManagerInterop is the bridge.
    auto interop = winrt::get_activation_factory<DataTransferManager, IDataTransferManagerInterop>();
    DataTransferManager manager{nullptr};
    if (FAILED(interop->GetForWindow(hwnd, winrt::guid_of<DataTransferManager>(),
                                     winrt::put_abi(manager))))
      return false;

    // GetForWindow returns the SAME per-HWND manager, so revoke the previous handler first.
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
