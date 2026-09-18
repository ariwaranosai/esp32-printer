#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
static std::string test_directory;
#define PHOTO_CLIENT_HOST_TEST
#define PHOTO_CACHE_DIR test_directory.c_str()
#include "../main/photo_client.cpp"

int main(int argc, char **argv) {
    assert(argc == 2);
    char directory[] = "/tmp/photopainter-cache-XXXXXX";
    assert(mkdtemp(directory));
    test_directory = directory;
    const std::string url = "http://192.168.31.133:11280/photo.jpg", token(32, 'x');
    assert(remote_photo::valid_settings(url, token));
    assert(!remote_photo::valid_settings("http://user:pass@host/photo.jpg", token));
    assert(!remote_photo::valid_settings(url, token + "\r\nInjected: value"));
    assert(!remote_photo::valid_settings("file:///sdcard/photo.jpg", token));
    assert(!remote_photo::valid_settings(url, "short"));
    const int64_t now = 1800000000;
    assert(!remote_photo::due(true, now, now+10799, 10800, false));
    assert(remote_photo::due(true, now, now+10800, 10800, false));
    assert(remote_photo::due(true, now, now, 10800, true));
    assert(remote_photo::due(true, now, now-1, 10800, false));
    assert(remote_photo::due(false, now, now, 10800, false));
    assert(remote_photo::due(true, 0, 100, 10800, false));
    std::ifstream input(argv[1], std::ios::binary);
    body.assign(std::istreambuf_iterator<char>(input), {});
    const auto jpeg = body;
    assert(!jpeg.empty());
    content_length = body.size();
    assert(refresh_remote_photo(url, token, 10800, false));
    assert(authorization == "Bearer " + token);
    const auto first = cached_remote_photo(url);
    assert(!first.empty());
    assert(refresh_remote_photo(url, token, 10800, false) && requests == 1);
    for (int error : {401, 404, 500, 503, 302}) {
        status = error;
        assert(!refresh_remote_photo(url, token, 10800, true));
        assert(cached_remote_photo(url) == first && std::filesystem::exists(first));
    }
    status = 200;
    body = "{\"error\":\"not a photo\"}"; content_length = body.size();
    assert(!refresh_remote_photo(url, token, 10800, true));
    body = jpeg.substr(0, jpeg.size()-10); content_length = body.size();
    assert(!refresh_remote_photo(url, token, 10800, true));
    body = jpeg; content_length = MAX_BYTES + 1;
    assert(!refresh_remote_photo(url, token, 10800, true));
    content_length = 0; complete = false;
    assert(!refresh_remote_photo(url, token, 10800, true));
    complete = true; fail_commit = true;
    assert(!refresh_remote_photo(url, token, 10800, true));
    assert(cached_remote_photo(url) == first);
    fail_commit = false; fail_open = true;
    assert(!refresh_remote_photo(url, token, 10800, true));
    assert(cached_remote_photo(url) == first);
    fail_open = false;
    // Chunked response (no Content-Length) and a manual refresh swap only after validation.
    assert(refresh_remote_photo(url, token, 10800, true));
    assert(cached_remote_photo(url) != first);
    assert(cached_remote_photo(url + "?other=1").empty());
    std::filesystem::remove_all(test_directory);
    puts("photo schedule, bearer auth, HTTP failures, corruption, two-slot cache and commit recovery passed");
}
