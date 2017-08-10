#include "browser.h"

#include <X11/Xutil.h>
#include "path_helper.h"

#include "include/base/cef_bind.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/views/cef_window.h"

CefMainArgs BrowserApp::cefMainArgs;
CefRefPtr<BrowserApp> BrowserApp::singleton (new BrowserApp());
std::unordered_map<std::string, std::function<std::shared_ptr<MyRenderProcessHandler> (CefRefPtr<CefBrowser> browser)>> BrowserApp::knownRenderHandlers;

static void RunFunction(std::function<void()> func) {
    func();
}

void BrowserApp::DoCefThread() {
    CefSettings settings;
    settings.no_sandbox = true;
    CefString(&settings.resources_dir_path) = PathHelper::findDataFile("libs/cef/res");
    CefString(&settings.locales_dir_path) = PathHelper::findDataFile("libs/cef/res/locales/");
    CefInitialize(cefMainArgs, settings, singleton.get(), nullptr);
    CefRunMessageLoop();
    CefShutdown();
}

void BrowserApp::RunWithContext(std::function<void()> contextCallback) {
    if (!singleton->cefThread.joinable()) {
        singleton->contextCallback = contextCallback;
        singleton->cefThread = std::thread(DoCefThread);
    } else if (!CefCurrentlyOn(TID_UI)) {
        RunOnUI(contextCallback);
    } else {
        contextCallback();
    }
}

void BrowserApp::RunOnUI(std::function<void()> function) {
    CefPostTask(TID_UI, base::Bind(&RunFunction, function));
}

void BrowserApp::Shutdown() {
    if (singleton->cefThread.joinable()) {
        CefPostTask(TID_UI, base::Bind(CefQuitMessageLoop));

        singleton->cefThread.join();
    }
}

BrowserApp::BrowserApp() {

}

void BrowserApp::SetRenderHandler(int browser, std::string const& handler) {
    browserRenderHandlers[browser] = knownRenderHandlers.at(handler)(renderProcessBrowsers.at(browser));
}

void BrowserApp::OnContextInitialized() {
    contextCallback();
}


bool BrowserApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefProcessId source_process,
                                                      CefRefPtr<CefProcessMessage> message) {
    if (browserRenderHandlers.count(browser->GetIdentifier()) > 0 && browserRenderHandlers.at(browser->GetIdentifier())->OnProcessMessageReceived(browser, source_process, message))
        return true;
    if (message->GetName() == "SetRenderHandler") {
        CefRefPtr<CefListValue> args = message->GetArgumentList();
        BrowserApp::singleton->SetRenderHandler(args->GetInt(0), args->GetString(1));
        return true;
    }
    return false;
}


void BrowserApp::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
    printf("BrowserApp::OnBrowserCreated %i\n", browser->GetIdentifier());
    renderProcessBrowsers[browser->GetIdentifier()] = browser;
}

void BrowserApp::OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) {
    printf("BrowserApp::OnBrowserDestroyed %i\n", browser->GetIdentifier());
    browserRenderHandlers.erase(browser->GetIdentifier());
    renderProcessBrowsers.erase(browser->GetIdentifier());
}

void BrowserApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefV8Context> context) {
    browserRenderHandlers.at(browser->GetIdentifier())->OnContextCreated(browser, frame, context);
}


void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browserList.push_back(browser);

    CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("SetRenderHandler");
    CefRefPtr<CefListValue> args = msg->GetArgumentList();
    args->SetInt(0, browser->GetIdentifier());
    args->SetString(1, renderHandlerId);
    browser->SendProcessMessage(PID_RENDERER, msg);
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    for (auto bit = browserList.begin(); bit != browserList.end(); ++bit) {
        if ((*bit)->IsSame(browser)) {
            browserList.erase(bit);
            break;
        }
    }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                                const CefString& errorText, const CefString& failedUrl) {
    if (errorCode == ERR_ABORTED)
        return;

    std::stringstream ss;
    ss << "<html><body><h2>Failed to load URL " << std::string(failedUrl) << "</h2>"
       << "<p>" << std::string(errorText) << " (" << errorCode << ").</p></body></html>";
    frame->LoadString(ss.str(), failedUrl);
}

void BrowserClient::CloseAllBrowsers(bool forceClose) {
    if (!CefCurrentlyOn(TID_UI)) {
        CefPostTask(TID_UI, base::Bind(&BrowserClient::CloseAllBrowsers, this, forceClose));
        return;
    }
    for (auto it = browserList.begin(); it != browserList.end(); ++it)
        (*it)->GetHost()->CloseBrowser(forceClose);
}

void MyWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
    window->AddChildView(browserView);
    window->SetPosition({options.x, options.y});
    window->SetSize({options.w, options.h});
    if (options.visible)
        window->Show();
    if (options.centerScreen) {
        XSizeHints sizehints;
        sizehints.win_gravity = CenterGravity;
        sizehints.flags = PWinGravity;
        XSetNormalHints(cef_get_xdisplay(), window->GetWindowHandle(), &sizehints);
    }

    browserView->RequestFocus();
}