#include "UpdateManager.h"

#include <string.h>

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <StreamString.h>
#include <Update.h>

#include "firmware_version.h"
#include "version_compare.h"
#include "../net/GitHubReleases.h"

extern const uint8_t github_certs_pem_start[]
    asm("_binary_data_github_certs_pem_start");
extern const uint8_t github_certs_pem_end[]
    asm("_binary_data_github_certs_pem_end");

namespace {

constexpr uint32_t kHttpTimeoutMs = 10000;
constexpr uint32_t kDownloadIdleTimeoutMs = 15000;
constexpr size_t   kOtaChunkSize = 1024;

void addGitHubDownloadHeaders(HTTPClient& http) {
    http.setUserAgent("claude-buddy-ota/1");
    http.setAcceptEncoding("identity");
    http.addHeader("Accept", "application/octet-stream");
    http.addHeader("Cache-Control", "no-cache");
}

void copyError(char* dst, size_t dst_len, const char* msg) {
    if (!dst || dst_len == 0) return;
    strncpy(dst, msg ? msg : "", dst_len - 1);
    dst[dst_len - 1] = 0;
}

void setHttpCodeError(char* dst, size_t dst_len,
                      const char* prefix, int code) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%s %d", prefix, code);
    copyError(dst, dst_len, msg);
}

void setUpdateError(char* dst, size_t dst_len, const char* prefix) {
    StreamString err;
    Update.printError(err);
    err.trim();

    char msg[64];
    if (err.length() > 0) {
        snprintf(msg, sizeof(msg), "%s: %.42s", prefix, err.c_str());
    } else {
        snprintf(msg, sizeof(msg), "%s", prefix);
    }
    copyError(dst, dst_len, msg);
}

bool isRedirect(int code) {
    return code == 301 || code == 302 || code == 303 ||
           code == 307 || code == 308;
}

}  // namespace

UpdateManager& UpdateManager::instance() {
    static UpdateManager s;
    return s;
}

void UpdateManager::begin() {
    state_ = State::Idle;
    bytes_received_ = 0;
    bytes_total_ = 0;
    last_error_[0] = 0;
    have_release_ = false;
    install_pending_ = false;
}

void UpdateManager::tick(uint32_t /*now_ms*/) {
    if (install_pending_) {
        install_pending_ = false;
        doInstallBlocking();
    }
}

void UpdateManager::requestCheck() {
    doCheckBlocking();
}

void UpdateManager::requestInstall() {
    if (state_ != State::UpdateAvailable) {
        copyError(last_error_, sizeof(last_error_), "no update available");
        state_ = State::Failed;
        return;
    }
    state_ = State::Downloading;
    bytes_received_ = 0;
    bytes_total_ = 0;
    install_pending_ = true;
}

UpdateManager::Status UpdateManager::status() const {
    Status s{};
    s.state = state_;
    s.bytes_received = bytes_received_;
    s.bytes_total = bytes_total_;
    strncpy(s.last_error, last_error_, sizeof(s.last_error) - 1);
    s.last_error[sizeof(s.last_error) - 1] = 0;
    return s;
}

const github_releases_parse::ReleaseInfo*
UpdateManager::latestRelease() const {
    return have_release_ ? &latest_ : nullptr;
}

const char* UpdateManager::currentVersion() const {
    return FIRMWARE_VERSION;
}

void UpdateManager::doCheckBlocking() {
    state_ = State::Checking;
    have_release_ = false;
    last_error_[0] = 0;

    net::FetchResult r = net::fetchLatestRelease();
    if (!r.ok) {
        copyError(last_error_, sizeof(last_error_), r.error);
        state_ = State::Failed;
        return;
    }

    latest_ = r.info;
    have_release_ = true;

    if (version_compare::isNewer(FIRMWARE_VERSION, latest_.tag)) {
        state_ = State::UpdateAvailable;
    } else {
        state_ = State::UpToDate;
    }
}

void UpdateManager::doInstallBlocking() {
    if (!have_release_ || !latest_.download_url[0]) {
        copyError(last_error_, sizeof(last_error_), "no release");
        state_ = State::Failed;
        return;
    }

    NetworkClientSecure client;
    // Calling setCACertBundle() with non-null bytes makes
    // NetworkClientSecure set _use_ca_bundle=true. esp_crt_bundle_set()
    // expects an IDF-specific binary format (not PEM), so our PEM here
    // is technically rejected — but esp_crt_bundle_attach() falls back
    // to the IDF-default Mozilla bundle, which covers everything we
    // need (DigiCert, ISRG Root X1, Amazon, etc.). Switching to a
    // generated binary bundle is future cleanup; this works today.
    const size_t bundle_size =
        (size_t)(github_certs_pem_end - github_certs_pem_start);
    client.setCACertBundle(github_certs_pem_start, bundle_size);

    // Manually resolve the GitHub asset redirect — HTTPUpdate's built-in
    // redirect follower has been unreliable with the cross-host TLS hop
    // from github.com to release-assets.githubusercontent.com.
    Serial.printf("[ota] resolving %s\n", latest_.download_url);
    String installUrl = latest_.download_url;
    {
        HTTPClient probe;
        probe.setTimeout(kHttpTimeoutMs);
        probe.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!probe.begin(client, installUrl)) {
            copyError(last_error_, sizeof(last_error_), "probe begin failed");
            state_ = State::Failed;
            return;
        }
        addGitHubDownloadHeaders(probe);
        const char* headerKeys[] = {"Location"};
        probe.collectHeaders(headerKeys, 1);
        int code = probe.GET();
        Serial.printf("[ota] probe code=%d\n", code);
        if (isRedirect(code)) {
            String loc = probe.header("Location");
            Serial.printf("[ota] redirect -> %.120s%s\n",
                          loc.c_str(), loc.length() > 120 ? "..." : "");
            if (loc.length() > 0) {
                installUrl = loc;
            } else {
                copyError(last_error_, sizeof(last_error_),
                          "no Location header");
                state_ = State::Failed;
                probe.end();
                return;
            }
        } else if (code != 200) {
            setHttpCodeError(last_error_, sizeof(last_error_),
                             "probe http", code);
            state_ = State::Failed;
            probe.end();
            return;
        }
        probe.end();
    }

    // Fresh TLS handshake for the resolved URL.
    client.stop();

    Serial.printf("[ota] downloading %.120s%s\n",
                  installUrl.c_str(),
                  installUrl.length() > 120 ? "..." : "");

    HTTPClient download;
    download.setTimeout(kHttpTimeoutMs);
    download.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!download.begin(client, installUrl)) {
        copyError(last_error_, sizeof(last_error_), "download begin failed");
        state_ = State::Failed;
        return;
    }
    addGitHubDownloadHeaders(download);

    int code = download.GET();
    int len = download.getSize();
    Serial.printf("[ota] download code=%d len=%d\n", code, len);
    if (code != HTTP_CODE_OK) {
        setHttpCodeError(last_error_, sizeof(last_error_),
                         "download http", code);
        state_ = State::Failed;
        download.end();
        return;
    }
    if (len <= 0) {
        copyError(last_error_, sizeof(last_error_), "no content length");
        state_ = State::Failed;
        download.end();
        return;
    }

    if (!Update.begin((size_t)len, U_FLASH)) {
        setUpdateError(last_error_, sizeof(last_error_), "ota begin failed");
        state_ = State::Failed;
        download.end();
        return;
    }

    NetworkClient* stream = download.getStreamPtr();
    if (!stream || stream->peek() != 0xE9) {
        copyError(last_error_, sizeof(last_error_), "bad firmware header");
        Update.abort();
        state_ = State::Failed;
        download.end();
        return;
    }

    uint8_t buf[kOtaChunkSize];
    size_t written = 0;
    uint32_t last_data_ms = millis();
    while (written < (size_t)len) {
        int avail = stream->available();
        if (avail > 0) {
            size_t remaining = (size_t)len - written;
            size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
            if ((size_t)avail < want) want = (size_t)avail;

            int got = stream->readBytes(buf, want);
            if (got <= 0) {
                copyError(last_error_, sizeof(last_error_), "download read failed");
                Update.abort();
                state_ = State::Failed;
                download.end();
                return;
            }

            size_t out = Update.write(buf, (size_t)got);
            if (out != (size_t)got) {
                setUpdateError(last_error_, sizeof(last_error_),
                               "ota write failed");
                Update.abort();
                state_ = State::Failed;
                download.end();
                return;
            }

            written += out;
            bytes_received_ = (uint32_t)written;
            bytes_total_ = (uint32_t)len;
            last_data_ms = millis();
            delay(1);
            continue;
        }

        if (!download.connected() ||
            (millis() - last_data_ms) > kDownloadIdleTimeoutMs) {
            copyError(last_error_, sizeof(last_error_), "download timeout");
            Update.abort();
            state_ = State::Failed;
            download.end();
            return;
        }
        delay(1);
    }

    if (!Update.end()) {
        setUpdateError(last_error_, sizeof(last_error_), "ota end failed");
        state_ = State::Failed;
        download.end();
        return;
    }
    if (!Update.isFinished()) {
        copyError(last_error_, sizeof(last_error_), "ota incomplete");
        state_ = State::Failed;
        download.end();
        return;
    }

    download.end();
    Serial.println("[ota] install OK; rebooting");
    state_ = State::InstallReady;
    delay(500);
    ESP.restart();   // never returns
}
