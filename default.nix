with import ../nix-system/toolchain.nix {};
stdenv.mkDerivation {
  name = "fbhax";
  src = builtins.filterSource (p: t: ! builtins.elem p ["default.nix" "result"]) ./.;
  dontStrip = true;
  dontCrossStrip = true;
  unpackPhase = ":";
  buildPhase = ''
    $CC $src/fb.c -I$src -o fbhax -g3 -ggdb -std=gnu11
  '';
  installPhase = ''
    mkdir -p $out/bin
    cp fbhax $out/bin
  '';
}
