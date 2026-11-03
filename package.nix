/*
  Package definition for material-decoration, a Material-ish window
  decoration theme for KWin (Plasma 6, KDecoration3).

  Built as a Qt6/KF6 CMake package; called with `callPackage` from the
  enclosing flake (see flake.nix), usually through `pkgs.kdePackages`.
*/
{
  lib,
  stdenv,
  cmake,
  ninja,
  gettext,
  dbus,
  nodejs,
  wayland,
  vulkan-headers,
  libx11,
  libxcb,
  kdePackages,
  gitUpdater,

  # Source tree to package. Defaults to the local tree (this repository),
  # with .git and other ignored files stripped, so it also works when the
  # flake is not used (e.g. for a future Nixpkgs submission).
  #
  # NB: called "source" rather than "src" so that a bare
  # `callPackage ./package.nix { }` does not pick up the unrelated `src`
  # package that exists at the top level of Nixpkgs.
  source ? lib.cleanSource ./.,
  # Latest release tag at packaging time (git describe: 26.11.02-4-gc9f8684).
  version ? "26.11.02",
  # Whether to build and run the CTest suite; used by the flake's checks.
  withTests ? false,
}:

stdenv.mkDerivation {
  pname = "material-decoration";
  inherit version;
  src = source;

  strictDeps = true;

  nativeBuildInputs =
    [
      cmake
      ninja
      gettext # msgfmt/msgmerge for ki18n_install(po)
      wayland # wayland-scanner, for the keystate protocol (optional feature)
      kdePackages.extra-cmake-modules
      kdePackages.wrapQtAppsHook
    ]
    ++ lib.optionals withTests [
      dbus # provides dbus-run-session, needed by several tests
      nodejs # runs the material-tiling KWin script test
    ];

  buildInputs =
    [
      vulkan-headers
      libx11
      libxcb
      wayland
    ]
    ++ (with kdePackages; [
      kdecoration
      plasma-wayland-protocols
      qtbase
      qtdeclarative # Qt6::QmlIntegration
      qttools # Qt6::UiTools and qdbusxml2cpp
      kconfig
      kconfigwidgets
      kcoreaddons
      kguiaddons
      ki18n
      kiconthemes
      kcmutils
      kservice
      kwidgetsaddons
      kwindowsystem
    ]);

  # Several tests are Qt applications; force offscreen for the ones that
  # don't set it themselves (sandboxed builds have no display).
  preCheck = lib.optionalString withTests ''
    export QT_QPA_PLATFORM=offscreen
  '';

  cmakeFlags = lib.optionals withTests [ (lib.cmakeBool "BUILD_TESTING" true) ];

  doCheck = withTests;

  passthru.updateScript = gitUpdater { };

  # The decoration plugin and the KCM are loaded by KWin/systemsettings at
  # runtime and are not meant to be started directly, so no mainProgram.
  meta = {
    description = "Material-ish window decoration theme for KWin, with Locally Integrated Menu and Menu Search";
    longDescription = ''
      Material-decoration is a window decoration theme for KWin that hides
      the AppMenu icon button and draws the menu in the titlebar instead,
      including a search button for menu actions. It works on both X11 and
      Wayland, ships a configuration module, and installs a companion
      "Material Tiling Popup" KWin script.
    '';
    homepage = "https://github.com/guiodic/material-decoration";
    changelog = "https://github.com/guiodic/material-decoration/releases";
    platforms = lib.platforms.linux;
    license = lib.licenses.gpl2Plus;
    maintainers = [ ];
  };
}
