/*
  flake.nix for oxygen-decoration-lim.

  Install it on NixOS from the repository:

      nix build          # -> ./result

  or install it into a profile:

      nix profile install .

  To use it from your NixOS configuration, add this flake as an input:

      # flake.nix of your system
      inputs.oxygen-decoration-lim = {
        url = "github:guiodic/oxygen-decoration-lim/master"; # or path: to this repo
        inputs.nixpkgs.follows = "nixpkgs";
      };

      # in your outputs, then in configuration.nix:
      environment.systemPackages = [
        oxygen-decoration-lim.packages.${system}.default
      ];

  KWin discovers window decoration plugins by scanning the Qt plugin
  directories (org.kde.kdecoration3) reachable through the system
  environment, which is covered for packages pulled in via the Plasma
  system environment. Select the
  "oxygen-decoration-lim" theme in System Settings > Colors & Themes > Window
  Decorations and log out/in afterwards (KWin must restart to load the
  plugin and the companion "Oxygen LIM Tiling Popup" script).
*/
{
  description = "Oxygen-styled window decoration theme for KWin (with locally integrated menus)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          oxygen-decoration-lim = pkgs.kdePackages.callPackage ./package.nix {
            source = self;
          };
        in
        {
          inherit oxygen-decoration-lim;
          default = oxygen-decoration-lim;
        }
      );

      checks = forAllSystems (
        system:
        let
          oxygen-decoration-lim = self.packages.${system}.oxygen-decoration-lim;
        in
        {
          build = oxygen-decoration-lim;
          # Runs the package's CTest suite (including the Qt unit tests and
          # the oxygen-lim-tiling KWin script test under a private DBus session).
          tests = oxygen-decoration-lim.override { withTests = true; };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];
            packages = [
              pkgs.gdb
              pkgs.clang-tools
            ];
          };
        }
      );
    };
}
