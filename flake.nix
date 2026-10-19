{
  inputs = {
    nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.zst";
  };

  outputs = {self, nixpkgs}:
  let
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
  in
  {
    packages.${system}.bk = pkgs.stdenv.mkDerivation {
      pname   = "bk";
      version = "0.1.0";
      src = ./.;
      buildInputs = [ pkgs.readline ];
      makeFlags = [ "PREFIX=$(out)" ];
    };
    devShells.${system}.default = pkgs.mkShell {
      packages = with pkgs; [ readline gcc gnumake];
    };
  };
}
