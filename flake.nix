{
  description = "ClassiCube Flake";

  # Nixpkgs / NixOS version to use.
  inputs.nixpkgs.url = "nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
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

            font_path = "${liberation_ttf}/share/fonts/truetype";

            enableParallelBuilding = true;

            postPatch = ''
              # ClassiCube hardcodes locations of fonts.
              # This changes the hardcoded location
              # to the path of liberation_ttf instead
              substituteInPlace src/Platform_Posix.c \
                --replace '%NIXPKGS_FONT_PATH%' "${font_path}"
              # ClassiCube's Makefile hardcodes JOBS=1 for some reason,
              # even though it works perfectly well multi-threaded.
              substituteInPlace src/Makefile \
                --replace 'JOBS=1' "JOBS=$NIX_BUILD_CORES"
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
