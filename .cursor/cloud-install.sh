#!/usr/bin/env bash
# Cursor Cloud Environment install for the Finwood zephyr + zephyr-devel workspace.
# Idempotent. Safe to re-run on agent boot / environment builds.
#
# Layout goal:
#   <parent>/zephyr              ← sibling checkout (Cloud multi-repo)
#   <parent>/zephyr-devel        ← west manifest / T2 workspace
#   <parent>/zephyr-devel/deps/zephyr → symlink to sibling zephyr
#
# Invoked by .cursor/environment.json (repo-file managed). The dashboard still
# selects the zephyr + zephyr-devel repo group; that is not expressible here.

set -euo pipefail

log() { printf 'cloud-install: %s\n' "$*"; }

# True if $2 looks like a checkout of repo $1.
# For "zephyr", require real Zephyr tree markers — zephyr-devel ships a module
# entrypoint at ./zephyr/ (CMakeLists.txt + module.yml) that must not win.
is_repo_candidate() {
	local name="$1"
	local d="$2"
	[[ -n "$d" && -d "$d" ]] || return 1
	if [[ "$name" == "zephyr" ]]; then
		[[ -f "$d/VERSION" || -f "$d/SDK_VERSION" || -f "$d/west.yml" ]]
		return $?
	fi
	[[ -d "$d/.git" || -f "$d/west.yml" || -f "$d/CMakeLists.txt" ]]
}

find_repo() {
	local name="$1"
	local d
	local parent
	parent="$(dirname "$PWD")"
	# Prefer multi-repo sibling checkouts over $PWD/<name>. When install cwd is
	# zephyr-devel, $PWD/zephyr is the module stub, not Finwood/zephyr.
	for d in \
		"${CURSOR_REPO_DIR:-}/$name" \
		"/agent/repos/$name" \
		"/workspace/$name" \
		"/workspace/repos/$name" \
		"${parent}/$name" \
		"$PWD/repos/$name" \
		"$PWD/$name"
	do
		if is_repo_candidate "$name" "$d"; then
			printf '%s\n' "$d"
			return 0
		fi
	done
	# If cwd itself is the repo
	if [[ "$(basename "$PWD")" == "$name" ]] && is_repo_candidate "$name" "$PWD"; then
		printf '%s\n' "$PWD"
		return 0
	fi
	return 1
}

# Remove a nested .git left under zephyr-devel/zephyr/ by a prior failed west
# update that mistook the module entrypoint for the Zephyr project path.
scrub_module_zephyr_git() {
	local devel="$1"
	local stub="${devel}/zephyr"
	if [[ -d "${stub}/.git" && -f "${stub}/module.yml" && ! -f "${stub}/VERSION" ]]; then
		log "removing nested .git under module entrypoint ${stub}"
		rm -rf "${stub}/.git"
	fi
}

ensure_uv() {
	if command -v uv >/dev/null 2>&1; then
		return 0
	fi
	log "installing uv"
	curl -LsSf https://astral.sh/uv/install.sh | sh
	export PATH="${HOME}/.local/bin:${PATH}"
	if [[ -x "${HOME}/.local/bin/uv" && ! -e /usr/local/bin/uv ]]; then
		sudo ln -sfn "${HOME}/.local/bin/uv" /usr/local/bin/uv || true
	fi
	command -v uv >/dev/null 2>&1
}

ensure_apt_deps() {
	local pkgs=(
		ninja-build
		device-tree-compiler
		gperf
		ccache
		gcc-multilib
		g++-multilib
		curl
		ca-certificates
		git
		python3
	)
	local missing=()
	local p
	for p in "${pkgs[@]}"; do
		if ! dpkg -s "$p" >/dev/null 2>&1; then
			missing+=("$p")
		fi
	done
	if ((${#missing[@]})); then
		log "installing apt packages: ${missing[*]}"
		sudo apt-get update -y
		sudo DEBIAN_FRONTEND=noninteractive apt-get install -y "${missing[@]}"
	else
		log "apt build deps already present"
	fi
}

wire_sibling_zephyr() {
	local devel="$1"
	local zephyr="$2"
	local dest="${devel}/deps/zephyr"

	mkdir -p "${devel}/deps"

	if [[ -L "$dest" ]]; then
		local target
		target="$(readlink -f "$dest" || true)"
		if [[ "$target" == "$(readlink -f "$zephyr")" ]]; then
			log "deps/zephyr already symlinked to sibling"
			return 0
		fi
		rm -f "$dest"
	elif [[ -d "$dest" ]]; then
		log "replacing nested deps/zephyr clone with sibling symlink"
		rm -rf "$dest"
	elif [[ -e "$dest" ]]; then
		rm -f "$dest"
	fi

	ln -sfn "$zephyr" "$dest"
	log "wired ${dest} -> ${zephyr}"
}

seed_west_manifest_rev() {
	local zephyr="$1"
	# west imports resolve west.yml via refs/heads/manifest-rev (not refs/west/).
	git -C "$zephyr" update-ref refs/heads/manifest-rev HEAD
	log "seeded refs/heads/manifest-rev at $(git -C "$zephyr" rev-parse --short HEAD)"
}

restore_zephyr_checkout() {
	local zephyr="$1"
	local sha="$2"
	local branch="$3"

	local head_sha head_branch
	head_sha="$(git -C "$zephyr" rev-parse HEAD)"
	head_branch="$(git -C "$zephyr" rev-parse --abbrev-ref HEAD)"

	if [[ "$head_sha" == "$sha" && "$head_branch" == "$branch" ]]; then
		return 0
	fi

	log "restoring zephyr checkout to ${branch:-detached}@${sha:0:12} (west moved it)"
	if [[ -n "$branch" && "$branch" != "HEAD" ]]; then
		git -C "$zephyr" checkout -B "$branch" "$sha"
	else
		git -C "$zephyr" checkout --detach "$sha"
	fi
}

write_bashrc_exports() {
	local zephyr_base="$1"
	local marker="# >>> zephyr-devel cloud env >>>"
	local end_marker="# <<< zephyr-devel cloud env <<<"
	local block

	block=$(cat <<EOF
${marker}
export ZEPHYR_BASE=${zephyr_base}
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export PATH="\${HOME}/.local/bin:\${PATH}"
${end_marker}
EOF
)

	touch "${HOME}/.bashrc"
	if grep -qF "$marker" "${HOME}/.bashrc"; then
		# Replace existing block
		local tmp
		tmp="$(mktemp)"
		awk -v m="$marker" -v e="$end_marker" '
			$0 == m {skip=1; next}
			$0 == e {skip=0; next}
			!skip {print}
		' "${HOME}/.bashrc" >"$tmp"
		cat "$tmp" >"${HOME}/.bashrc"
		rm -f "$tmp"
	fi
	printf '\n%s\n' "$block" >>"${HOME}/.bashrc"
	log "wrote ZEPHYR_* exports to ~/.bashrc"
}

ensure_sdk() {
	local devel="$1"
	local zephyr_base="$2"

	if compgen -G "/opt/zephyr-sdk-*/" >/dev/null; then
		log "Zephyr SDK already present under /opt"
		return 0
	fi

	log "installing Zephyr SDK under /opt (from SDK_VERSION)"
	sudo mkdir -p /opt
	sudo chown "$(id -u):$(id -g)" /opt || true

	export ZEPHYR_BASE="$zephyr_base"
	export ZEPHYR_SDK_INSTALL_DIR=/opt/
	export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

	# native_sim + common ARM boards; keep the download bounded for Cloud Builds.
	(
		cd "$devel"
		uv run west sdk install -b /opt -t x86_64-zephyr-elf -t arm-zephyr-eabi
	)
}

main() {
	ensure_apt_deps
	ensure_uv
	export PATH="${HOME}/.local/bin:${PATH}"

	local devel zephyr
	devel="$(find_repo zephyr-devel)" || {
		log "ERROR: could not find zephyr-devel checkout"
		exit 1
	}
	zephyr="$(find_repo zephyr)" || {
		log "ERROR: could not find sibling zephyr checkout"
		exit 1
	}

	log "zephyr-devel=${devel}"
	log "zephyr=${zephyr}"

	scrub_module_zephyr_git "$devel"
	wire_sibling_zephyr "$devel" "$zephyr"

	local zephyr_sha zephyr_branch
	zephyr_sha="$(git -C "$zephyr" rev-parse HEAD)"
	zephyr_branch="$(git -C "$zephyr" rev-parse --abbrev-ref HEAD)"

	seed_west_manifest_rev "$zephyr"

	(
		cd "$devel"
		log "uv sync"
		uv sync
		# west update fetches imported HALs and refreshes west metadata.
		# It may detach deps/zephyr; we restore the sibling SHA/branch afterwards.
		log "west update"
		uv run west update --narrow --fetch-opt=--depth=1
	)

	restore_zephyr_checkout "$zephyr" "$zephyr_sha" "$zephyr_branch"
	seed_west_manifest_rev "$zephyr"

	local zephyr_base="${devel}/deps/zephyr"
	write_bashrc_exports "$zephyr_base"
	# shellcheck disable=SC1090
	source "${HOME}/.bashrc" || true
	export ZEPHYR_BASE="$zephyr_base"
	export ZEPHYR_SDK_INSTALL_DIR=/opt/
	export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

	ensure_sdk "$devel" "$zephyr_base"

	log "done"
	log "ZEPHYR_BASE=${ZEPHYR_BASE}"
	command -v uv
	uv run --directory "$devel" west list || true
}

main "$@"
