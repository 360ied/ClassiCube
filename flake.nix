{
  description = "ClassiCube Flake";

  # Nixpkgs / NixOS version to use.
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
    cef.url = "github:SpiralP/classicube-cef-plugin";
  };

  outputs =
    {
      self,
      nixpkgs,
      cef,
    }:
    let
      # System types to support.
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

      # Nixpkgs instantiated for supported system types.
      nixpkgsFor = forAllSystems (
        system:
        import nixpkgs {
          inherit system;
          overlays = [ self.overlay ];
        }
      );

    in
    {

      # A Nixpkgs overlay.
      overlay = final: prev: {

        classicube-b =
          with final;
          stdenv.mkDerivation rec {
            pname = "classicube-b";
            version = "0.0.1";

            src = ./.;

            nativeBuildInputs = [
              makeWrapper
              copyDesktopItems
              keepBuildTree
            ];

            desktopItems = [
              (makeDesktopItem {
                name = pname;
                desktopName = pname;
                genericName = "Sandbox Block Game";
                exec = "ClassiCube";
                icon = "CCicon";
                comment = "Minecraft Classic inspired sandbox game";
                categories = [ "Game" ];
              })
            ];

            enableParallelBuilding = true;

            fontPath = "${liberation_ttf}/share/fonts/truetype";
            cefPath = cef.packages.${system}.default;

            postPatch = ''
              # ClassiCube hardcodes locations of fonts.
              # This changes the hardcoded location
              # to the path of liberation_ttf instead
              substituteInPlace src/Platform_Posix.c \
                --replace-fail '%NIXPKGS_FONT_PATH%' "${fontPath}"

              # ClassiCube searches for plugins in the current directory.
              # This changes it to the path of cef
              substituteInPlace src/Game.c \
                --replace-fail '%NIXPKGS_PLUGINS_PATH%' "${cefPath}/plugins"

              pwd
              echo "${cefPath}"
            '';

            buildInputs = [
              xorg.libX11
              xorg.libXi
              libGL
              curl
              openal
              liberation_ttf
            ];

            installPhase = ''
              runHook preInstall

              mkdir -p "$out/bin"
              cp 'ClassiCube' "$out/bin"
              # ClassiCube puts downloaded resources
              # next to the location of the executable by default.
              # This doesn't work with Nix
              # as the location of the executable is read-only.
              # We wrap the program to make it put its resources
              # in ~/.local/share instead.
              wrapProgram "$out/bin/ClassiCube" \
                --run 'mkdir -p "$HOME/.local/share/ClassiCube"' \
                --set 'GLIBC_TUNABLES' 'glibc.rtld.optional_static_tls=16384' \
                --run 'cd       "$HOME/.local/share/ClassiCube"'

              mkdir -p "$out/share/icons/hicolor/256x256/apps"
              cp misc/CCicon.png "$out/share/icons/hicolor/256x256/apps"

              runHook postInstall
            '';

            meta = with lib; {
              homepage = "https://www.classicube.net/";
              description = "A lightweight, custom Minecraft Classic/ClassiCube client with optional additions written from scratch in C";
              license = licenses.bsd3;
              platforms = platforms.linux;
              maintainers = with maintainers; [ _360ied ];
              mainProgram = "ClassiCube";
            };

          };

      };

      # Provide some binary packages for selected system types.
      packages = forAllSystems (system: {
        inherit (nixpkgsFor.${system}) classicube-b;
      });

      # The default package for 'nix build'. This makes sense if the
      # flake provides only one package or there is a clear "main"
      # package.
      defaultPackage = forAllSystems (system: self.packages.${system}.classicube-b);
    };
}
