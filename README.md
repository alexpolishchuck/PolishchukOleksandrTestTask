# HTTPS API Client

A Windows C++20 console application that communicates with an HTTPS API using Boost.Asio, Boost.Beast, and OpenSSL.

## Requirements

- Windows 10/11
- **Visual Studio 2026** with **Desktop development with C++** and a Windows SDK
- MSVC with **C++20** support

### vcpkg packages

The client uses the following libraries:

| Package | Purpose |
| --- | --- |
| `boost-asio` | Asynchronous I/O and coroutines |
| `boost-beast` | HTTP requests and responses |
| `boost-json` | JSON serialization and parsing |
| `boost-url` | URL parsing and query parameters |
| `boost-uuid` | Generating event UUIDs |
| `openssl` | TLS/HTTPS support |

The project has been developed against **Boost 1.92.0** and **OpenSSL 3.6.3#0**. If the manifest uses vcpkg version overrides (for example `1.92.0#0`), keep them in `vcpkg.json` and use a vcpkg registry/baseline that contains those versions.

## Build with Visual Studio

1. Clone or download the repository.
2. Set up vcpkg if it is not installed already:

   ```bash:
   cd C:/
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg
   ./bootstrap-vcpkg.bat
   ```

3. Open the project's **`.sln`** or **`.vcxproj`** in Visual Studio. The exact solution filename depends on your repository.
4. In **Project Properties**, verify:
   - **Configuration:** `Debug` or `Release`
   - **Platform:** `x64`
   - **C/C++ → Language → C++ Language Standard:** `ISO C++20 Standard (/std:c++20)`
   - **Configuration Properties → Vcpkg:** manifest integration is enabled
   - **C/C++ → Command Line → Additional Options:** `/bigobj` (recommended when compiling Boost-heavy translation units)
5. Build using **Build → Build Solution** (`Ctrl+Shift+B`).

## Configuration

Create a configuration JSON file containing the API URL and API key:

```json
{
  "url": "https://example.com/api/v4",
  "api_key": "YOUR_API_KEY"
}
```
The default name for the config file is "config.json". Alternatively, it is also possible to pass the path to the config file as a parameter to the application, for example:
```bash
Client.exe "C:\Configs\client.json"
```

## Run

1. In Visual Studio, select the console application as the **Startup Project**.
2. Start with **Debug → Start Without Debugging** (`Ctrl+F5`), or run the built `.exe` from a terminal.
3. At the prompt, enter the config file path. Press **Enter** to use `config.json` in the application's **working directory**.

4. Use the console menu:

   ```text
   ========== HTTPS CLIENT ==========
   1. Server status
   2. Create event
   3. Upload image
   4. List events
   0. Exit
   ==================================
   ```

### Operations

- **Server status:** Sends a `GET` request to the status endpoint.
- **Create event:** Sends event information as JSON and displays the returned event ID.
- **Upload image:** Prompts for an event ID and image path, then uploads the image as `multipart/form-data`. The supplied implementation supports `.jpg`, `.jpeg`, and `.png` extensions.
- **List events:** Prompts for limit, offset, and image-uploaded filter, then prints returned events.

The client sends the API key in the `X-API-Key` header.

## Troubleshooting

**`fatal error C1128: number of sections exceeded object file format limit`**  
Enable `/bigobj` in **Project Properties → C/C++ → Command Line → Additional Options** and rebuild.

**`boost/url.hpp`, `boost/json.hpp`, or other headers cannot be found**  
Check `vcpkg.json`, install the missing ports, verify that the Visual Studio project uses vcpkg integration, and ensure the triplet matches the project platform (`x64-windows` for `x64`).

**OpenSSL certificate verification fails**  
The client verifies the server certificate. Ensure that OpenSSL has access to a trusted CA certificate store or configure a CA bundle explicitly.

**`config.json` cannot be opened**  
Check the supplied path and the application's current working directory. In Visual Studio, this can be adjusted under **Project Properties → Debugging → Working Directory**.
