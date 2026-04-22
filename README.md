# <img src="./resources/icons/sc-apps-ghostwriter.svg" align="left" width="32" style="padding-right:5px"> ghostwriter

*ghostwriter* is a Windows and Linux text editor for Markdown, which is a plain text markup format created by John Gruber. For more information about Markdown, please visit John Gruber’s website at <http://www.daringfireball.net>.  *ghostwriter* provides a relaxing, distraction-free writing environment, whether your masterpiece be that next blog post, your school paper, or your NaNoWriMo novel.  For a tour of its features, please visit the [*ghostwriter* project site](https://ghostwriter.kde.org).

## Screenshots

You can view screenshots of the application at [*ghostwriter's* project site](https://ghostwriter.kde.org).

## Documentation

A quick reference guide is available [here](https://ghostwriter.kde.org/documentation/).

## Installation

### Windows

An installer will be forthcoming at the [KDE binary factory](https://binary-factory.kde.org/), along with a nightly build.

### Linux

Versions of *ghostwriter* 2.2.0 and above are provided with KDE Gears releases and should be available with your Linux distribution.  For example, on Ubuntu, you can enter the following commands from your terminal:

    $ sudo apt update
    $ sudo apt install ghostwriter

On Fedora, enter the following commands instead:

    $ sudo dnf install ghostwriter

You may also find packages on the author's personal repository locations version 2.1.6 in case your GNU/Linux distribution is behind.  If you are running Ubuntu or one of its derivatives (Linux Mint, Xubuntu, etc.), open a terminal, and enter the following commands:

    $ sudo add-apt-repository ppa:wereturtle/ppa
    $ sudo apt update
    $ sudo apt install ghostwriter

Fedora users can install older version of *ghostwriter* from [Copr](https://copr.fedorainfracloud.org/) by opening a terminal and entering the following commands:

    $ sudo dnf copr enable wereturtle/stable
    $ sudo dnf install ghostwriter

Finally, you may follow the build instructions below to install on Linux with the latest source code.

### MacOS

An installer is planned in the future and will be hosted at the [KDE binary factory](https://binary-factory.kde.org/), along with a nightly build.  If you have any expertise to offer, please consider helping with a [Craft configuration](https://community.kde.org/Craft).

## Build

This tree targets **Qt 6.5+**, **KDE Frameworks 6**, **C++20**, and **CMake** (see [CMakeLists.txt](CMakeLists.txt)). The [CMakePresets.json](CMakePresets.json) file uses the **Ninja** generator.

Bundled third-party code (for example **cmark-gfm** under `3rdparty/`) is built with the application; you do not install it separately.

### All platforms (configure and compile)

From the repository root, after dependencies are installed and visible to CMake (see below):

    cmake --preset dev
    cmake --build build

For an optimized build:

    cmake --preset release
    cmake --build build-release

Install (optional; may require elevated permissions depending on the prefix):

    cmake --install build-release

If CMake cannot find Qt or KF6, set `CMAKE_PREFIX_PATH` to the install roots (semicolon-separated on Windows), for example the Craft root or Qt installation directory.

### Windows

1. **Compiler and SDK**  
   Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community is fine) with the workload **Desktop development with C++** (MSVC toolset and Windows SDK).

2. **CMake and Ninja**  
   Install [CMake](https://cmake.org/download/) (3.16 or newer) and [Ninja](https://ninja-build.org/) (or install them via the Visual Studio installer). **Git** is required to clone the repository.

3. **Qt 6 with WebEngine**  
   Use the [Qt Online Installer](https://www.qt.io/download-open-source). Install **Qt 6.5 or newer** for **MSVC 64-bit**, and include **Qt WebEngine** (needed for `QtWebEngineWidgets` and the preview). WebEngine pulls in a large dependency set; that is expected.

4. **KDE Frameworks 6 and ECM**  
   On Windows, KF6 is most often provided via **[KDE Craft](https://community.kde.org/Craft)**. Follow [Developing KDE software on Windows](https://community.kde.org/Get_Involved/development/Windows): use Craft to build or install the frameworks this project needs (**ECM**, **KCoreAddons**, **KConfigWidgets**, **KWidgetsAddons**, **KXmlGui**, **Sonnet**), then open a shell where Craft environment variables (and `CMAKE_PREFIX_PATH`) point at the Craft install and Qt.

5. **Configure**  
   From a **x64 Native Tools Command Prompt for VS 2022** (or any shell where `cl`, `cmake`, and Ninja work and Qt/KF6 are on `CMAKE_PREFIX_PATH`):

       cmake --preset dev
       cmake --build build

**Fullscreen / GPU note (Qt 6 on Windows):** `QWebEngineView` can force OpenGL-backed compositing. In full-screen mode, some Windows setups show a bug where menus no longer appear. If that happens, try launching with `--disable-gpu` (see **Command Line Usage** below). For day-to-day writing in windowed mode this is often unnecessary.

**WSL2:** You can build a **Linux** binary inside WSL using the Linux dependency list below; that binary is not a substitute for a native Windows `.exe`.

### Linux

Install a C++20 compiler, CMake, Ninja, Extra CMake Modules, Qt 6 development packages, and KDE Frameworks 6 development packages. If CMake reports a missing package, install the matching `-dev` / `-devel` package from your distribution.

**Debian / Ubuntu (24.04 or newer recommended for KF6 packages):**

    sudo apt update
    sudo apt install build-essential cmake ninja-build extra-cmake-modules \
      qt6-base-dev qt6-svg-dev qt6-webengine-dev qt6-webchannel-dev qt6-tools-dev \
      libkf6coreaddons-dev libkf6configwidgets-dev libkf6sonnet-dev \
      libkf6widgetsaddons-dev libkf6xmlgui-dev

**Fedora:**

    sudo dnf install gcc-c++ cmake ninja-build extra-cmake-modules \
      qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebengine-devel \
      kf6-kcoreaddons-devel kf6-kconfigwidgets-devel kf6-kwidgetsaddons-devel \
      kf6-kxmlgui-devel kf6-sonnet-devel

Then build using the **All platforms** commands above (`cmake --preset dev`, etc.).

### MacOS

1. Install **Xcode** or the **Command Line Tools**, plus **CMake** and **Ninja** (for example via [Homebrew](https://brew.sh/): `brew install cmake ninja`).
2. Install **Qt 6** (for example `brew install qt`). Ensure CMake can find it (`CMAKE_PREFIX_PATH` if needed).
3. Install **KDE Frameworks 6** and **ECM**. Prebuilt KF6 stacks on macOS are less uniform than on Linux; many developers use **[KDE Craft](https://community.kde.org/Craft)** or follow [Developing KDE software on Mac](https://community.kde.org/Get_Involved/development/Mac) so that `CMAKE_PREFIX_PATH` includes Qt and KF6.
4. From the repository root, run the **All platforms** CMake commands.

### FreeBSD

Prerequisites

* Git (`git` or `git-lite`)

Install the dependencies

    sudo pkg install cmake ninja kf6-extra-cmake-modules kf6-kconfigwidgets kf6-kcoreaddons kf6-kdoctools kf6-kwidgetsaddons kf6-kxmlgui kf6-sonnet qt6-base qt6-svg qt6-tools qt6-webengine

Get the sources

    git clone https://invent.kde.org/office/ghostwriter

Build

    $ cd ghostwriter
    $ mkdir build
    $ cd build
    $ cmake ..
    $ make
    $ sudo make install

## Building release binaries for Windows, Linux, and macOS

Native **Qt + KF6** applications are almost always **built on each target OS** (or in a CI runner for that OS). Cross-compiling this project from one machine to all three is not a supported path. A practical approach is a **CI matrix** (for example GitHub Actions jobs on `windows-latest`, `ubuntu-24.04`, and `macos-latest`), each installing that platform’s dependencies and running `cmake --preset release` plus your packager (installer, AppImage, `.dmg`, etc.).

This repository’s **Ubuntu** workflow (`.github/workflows/main.yml`) builds on every push using distribution packages. The **macOS** workflow (`.github/workflows/macos-build.yml`) is **manual** (`workflow_dispatch`): set the repository Actions variable **`KF6_CMAKE_PREFIX_PATH`** to a prefix that contains your KF6 installation (for example a [Craft](https://community.kde.org/Craft) root), then run the workflow from the Actions tab.

## Command Line Usage

For terminal users, *ghostwriter* can be run from the command line.  In your terminal window, simply type the following:

    $ ghostwriter myfile.md

where `myfile.md` is the path to your Markdown text file.

An option to disable GPU acceleration `--disable-gpu` is also available.  Simply type the following:

    $ ghostwriter --disable-gpu

A scenario where you may consider using software rendering would be if compiling against Qt 6 on Windows, and running the application in full screen mode.  See the documented note under the Windows build instructions above for further details.  Note that the application may inconsistently launch on Windows with GPU acceleration disabled, and it may take several attempts before you can start it successfully.

## Additional Markdown Processors

*ghostwriter* has built-in support for the cmark-gfm processor.  However, it also can auto-detect Pandoc, MultiMarkdown, or cmark processors.  To use any or all of the latter three, simply install them and ensure that their installation locations are added to your system's `PATH` environment variable.  *ghostwriter* will auto-detect their installation on startup, and give you live HTML preview and export options accordingly.

## Contribute

Please read the [contributing guide](https://ghostwriter.kde.org/contribute/) on how to contribute.  Your help would be much appreciated!

## Licensing

The source code for *ghostwriter* is licensed under the [GNU General Public License Version 3](http://www.gnu.org/licenses/gpl.html).  However, various icons and third-party FOSS code (i.e., cmark-gfm, MathJax, etc.) have different licenses compatible with GPLv3.  Please read the COPYING or LICENSE files in the respective folders for the different licenses.
