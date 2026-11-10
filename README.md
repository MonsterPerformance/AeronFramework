# AeronFramework

Static-library build of [Aeron](https://github.com/real-logic/aeron) (C client, C++ wrapper and C media driver, version `1.55.0-SNAPSHOT`) for Linux and C++23. One CMake project builds everything into a single `AeronGateway` library plus a standalone driver binary and a small stat tool.

| Target           | Kind               | Built from                                                                       | Purpose                                         |
| ---------------- | ------------------ | -------------------------------------------------------------------------------- | ----------------------------------------------- |
| `AeronGateway`   | static library     | `aeron-client/src/main/c`, `aeron-client/src/main/cpp_wrapper`, `aeron-driver/src/main/c` | C client, C++ wrapper and embeddable driver     |
| `aeron_driver`   | executable         | `aeron-driver/src/main/c/aeronmd.c`                                              | standalone media driver                         |
| `aeron-stat`     | executable         | `aeron-stat/src/main.cpp`                                                        | lists channels registered in the driver's `cnc.dat` |
| `client_example` | executable         | `examples/client.cpp`                                                            | build smoke test (C++23 `std::expected`)        |
| `server_example` | executable         | `examples/server.cpp`                                                            | build smoke test (C++23 `std::format`)          |

The driver sources are part of `AeronGateway` (only `aeronmd.c`, the `main` of the driver, is left out), so an application can link the library and start an embedded driver, or run `aeron_driver` as a separate process.

Tests from upstream live in `aeron-client/src/test/c` and `aeron-driver/src/test/c`. They are not wired into the top-level `CMakeLists.txt`.

## Rules

- Linux only: the library links `rt` and `pthread`, and `aeron_windows.c` is excluded from the build.
- A media driver must be running (standalone or embedded) before a client connects. Client and driver have to agree on the Aeron directory (`/dev/shm/aeron-<user>` by default).
- The driver is configured through environment variables: the Java property name with `.` replaced by `_`, upper-cased. `aeron.term.buffer.length` becomes `AERON_TERM_BUFFER_LENGTH`. See `aeron-driver/src/main/c/README.md`.
- Build type defaults to `Release` when none is given.
- Version macros (`AERON_VERSION_TXT`, `_MAJOR`, `_MINOR`, `_PATCH`, `_GITSHA`) are fixed in the top-level `CMakeLists.txt`; the git sha reads `custom-static-build`.
- `_DEFAULT_SOURCE` and `HAVE_STRUCT_MMSGHDR` are defined for every target, so `sendmmsg` / `recvmmsg` are used.

## Examples

Publish and subscribe over IPC with the C++ wrapper (start `aeron_driver` first):

```cpp
#include <Aeron.h>

#include <array>
#include <cstring>
#include <thread>

int main()
{
    aeron::Context ctx;
    auto client = aeron::Aeron::connect(ctx);

    const auto pubId = client->addPublication("aeron:ipc", 1001);
    const auto subId = client->addSubscription("aeron:ipc", 1001);

    auto pub = client->findPublication(pubId);
    auto sub = client->findSubscription(subId);
    while (!pub || !sub)
    {
        std::this_thread::yield();
        pub = client->findPublication(pubId);
        sub = client->findSubscription(subId);
    }

    std::array<std::uint8_t, 64> buf{};
    aeron::concurrent::AtomicBuffer atomic(buf.data(), buf.size());
    const char msg[] = "hello";
    atomic.putBytes(0, reinterpret_cast<const std::uint8_t*>(msg), sizeof msg);

    while (pub->offer(atomic, 0, sizeof msg) < 0)
    {
        std::this_thread::yield();
    }

    while (sub->poll(
        [](const aeron::concurrent::AtomicBuffer& b, aeron::util::index_t off,
           aeron::util::index_t len, const aeron::Header&)
        {
            /* use b.buffer() + off, len bytes */
        }, 10) == 0)
    {
        std::this_thread::yield();
    }
}
```

`examples/` currently holds two placeholder programs that only check that the toolchain builds C++23 and links `AeronGateway`; they do not talk to a driver yet.

Inspect what is registered in a running driver:

```
./build/aeron-stat
```

`aeron-stat` scans the driver's `cnc.dat` for `aeron:` channel strings. The path is compiled in (`/dev/shm/aeron-<user>/cnc.dat`), so edit `aeron-stat/src/main.cpp` for another user or Aeron directory.

## Build

```
cmake -S . -B build
cmake --build build -j
```

Binaries land in `build/`: `aeron_driver`, `aeron-stat`, `client_example`, `server_example`, and `libAeronGateway.a`.

Requires CMake 3.20+, a C++23 compiler (GCC 13+ or Clang 17+, because the examples use `<format>` and `<expected>`) and a C compiler with C11 support.

## Run the driver

Directly:

```
./build/aeron_driver
```

As a systemd service: see [Run the driver as a daemon](#run-the-driver-as-a-daemon) and [`daemonizer.sh`](daemonizer.sh).

## Run the driver as a daemon

[`daemonizer.sh`](daemonizer.sh) in the repository root creates, loads and starts a systemd unit named `aeron-driver.service`. It needs Linux with systemd and `sudo`.

**1. Install the binary.** The unit starts `/usr/local/bin/aeron_driver`, so install the project first:

```
cmake --install build --prefix /usr/local
```

**2. Edit the two machine-specific lines in [`daemonizer.sh`](daemonizer.sh):**

| Line               | Default         | Set it to                                                                 |
| ------------------ | --------------- | ------------------------------------------------------------------------- |
| `User=`            | `root`          | the account your Aeron clients run as (might need another user name)      |
| `CPUAffinity=`     | `6`             | the core the driver is pinned to (`0` to `nproc - 1`); a space-separated list or a range such as `2-3` also works |

The driver creates its Aeron directory as `/dev/shm/aeron-<user>`, named after the user it runs as. Clients must use the same directory, so run them as the same user or point both sides at one directory with `AERON_DIR`. `aeron-stat` also reads that path.

**3. Run the script:**

```
bash daemonizer.sh
```

It does three things, each with `sudo`:

1. writes `/etc/systemd/system/aeron-driver.service` (the unit is described below),
2. runs `systemctl daemon-reload` so systemd picks up the new file,
3. runs `systemctl enable --now aeron-driver.service`, which starts the driver immediately and on every boot.

The generated unit:

```
[Unit]
Description=Aeron Media Driver Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/aeron_driver
Restart=always
RestartSec=5
User=<your user>
CPUAffinity=<your core>

[Install]
WantedBy=multi-user.target
```

`Restart=always` with `RestartSec=5` brings the driver back five seconds after any exit, including a crash or a manual `kill`. Use `systemctl stop` to stop it for good (see below).

**4. Check that it runs:**

```
systemctl status aeron-driver.service
journalctl -u aeron-driver.service -f
ls /dev/shm/aeron-<user>/cnc.dat
```

**Configure the driver.** Add environment variables to the unit and reload, for example:

```
sudo systemctl edit aeron-driver.service
```

```
[Service]
Environment=AERON_TERM_BUFFER_LENGTH=16777216
```

```
sudo systemctl restart aeron-driver.service
```

**Everyday commands:**

```
sudo systemctl restart aeron-driver.service    # after upgrading the binary or changing config
sudo systemctl stop aeron-driver.service       # stop until next boot
sudo systemctl disable --now aeron-driver.service   # stop and do not start on boot
```

To remove the service completely, disable it as above, then `sudo rm /etc/systemd/system/aeron-driver.service` and `sudo systemctl daemon-reload`.

Notes:

- Running [`daemonizer.sh`](daemonizer.sh) again overwrites the unit file, so put long-lived changes in a `systemctl edit` drop-in rather than in the generated file.
- The script uses the fixed unit name `aeron-driver`, so it manages a single driver per machine.
- `CPUAffinity` only pins the driver process. To keep other work off that core, isolate it (for example with the `isolcpus` kernel parameter) and pin your application threads to different cores.

## Use from another project

After installing (`cmake --install build --prefix /usr/local`), the library goes to `lib/`, the driver to `bin/`, and all client and wrapper headers to `include/aeron/`:

```
find_package(Aeron REQUIRED)
target_link_libraries(app PRIVATE Aeron::AeronGateway m)
```

`Aeron::AeronGateway` carries `include/aeron` as its include directory, so `#include <Aeron.h>` (C++ wrapper) and `#include <aeronc.h>` (C API) both work. Applications link `m` explicitly, as the targets in this repo do.

or from a checkout:

```
add_subdirectory(AeronFramework)
target_link_libraries(app PRIVATE AeronGateway m)
```

## Layout

```
aeron-client/src/main/c/            C client (aeronc.h), concurrent, collections, protocol, util
aeron-client/src/main/cpp_wrapper/  C++ wrapper (Aeron.h, Publication.h, Subscription.h, ...)
aeron-client/src/test/c/            upstream client tests (not built by default)
aeron-driver/src/main/c/            C media driver: conductor, sender, receiver, media/, uri/, agent/
aeron-driver/src/test/c/            upstream driver tests (not built by default)
aeron-stat/src/                     aeron-stat tool
examples/                           client.cpp  server.cpp
daemonizer.sh                       systemd unit for the driver
CMakeLists.txt                      single top-level build
```

## License

Aeron is Copyright Real Logic Limited and licensed under the Apache License 2.0. The sources in `aeron-client` and `aeron-driver` keep their original headers.
