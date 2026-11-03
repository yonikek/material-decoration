/*
  flake.nix for material-decoration.

  Install it on NixOS from the repository:

      nix build          # -> ./result

  or install it into a profile:

      nix profile install .

  To use it from your NixOS configuration, add this flake as an input:

      # flake.nix of your system
      inputs.material-decoration = {
        url = "github:guiodic/material-decoration/master"; # or path: to this repo
        inputs.nixpkgs.follows = "nixpkgs";
      };

      # in your outputs, then in configuration.nix:
      environment.systemPackages = [
        material-decoration.packages.${system}.default
      ];

  KWin discovers window decoration plugins by scanning the Qt plugin
  directories (org.kde.kdecoration3) reachable through the system
  environment, which is covered for packages pulled in via the Plasma
  system environment. Select the
  "Material" theme in System Settings > Colors & Themes > Window
  Decorations and log out/in afterwards (KWin must restart to load the
  plugin and the companion "Material Tiling Popup" script).
*/
{
  description = "Material-ish window decoration theme for KWin";

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
          material-decoration = pkgs.kdePackages.callPackage ./package.nix {
            source = self;
          };
        in
        {
          inherit material-decoration;
          default = material-decoration;
        }
      );

      checks = forAllSystems (
        system:
        let
          material-decoration = self.packages.${system}.material-decoration;
        in
        {
          build = material-decoration;
          # Runs the package's CTest suite (including the Qt unit tests and
          # the material-tiling KWin script test under a private DBus session).
          tests = material-decoration.override { withTests = true; };
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
