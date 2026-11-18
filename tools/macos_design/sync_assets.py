import argparse
import gzip
from io import BytesIO
import json
from pathlib import Path
import subprocess
import struct
import zlib
from urllib.parse import quote

from PIL import Image

from sync_palette import ROOT, REVISION


ASSETS = {
    "contacts": ("Icon_TabContacts", "ic_contacts.png", "ic_contacts@2x.png"),
    "calls": ("Icon_TabRecentCalls", "ic_calls.png", "ic_calls@2x.png"),
    "chats": ("Icon_TabChatList", "ic_chats.png", "ic_chats@2x.png"),
    "settings": ("Icon_TabSettings", "ic_settings.png", "ic_settings@2x.png"),
    "compose": ("Icon_NewMessage", "ic_create.png", "ic_create@2x.png"),
    "attach": ("Icon_ChatAttach", "ic_attach@1x.png", "ic_attach@2x.png"),
    "send": ("Icon_SendMessage", "ic_send@1x.png", "ic_send@2x (1).png"),
    "search": ("Icon_SearchChatMessages", "ic_search.png", "ic_search@2x.png"),
    "more": ("Icon_ChatActions", "ic_more.png", "ic_more@2x.png"),
    "call": ("Icon_callNavigationHeader", "ic_call.png", "ic_call@2x.png"),
    "next": ("Icon_GeneralNext", "ArrowGrey.png", "ArrowGrey@2x.png"),
    "qr": ("Icon_WalletQR", "ic_qr.png", "ic_qr@2x.png"),
    "add_account": ("Icon_Account_Add_Account", "person.png", "person@2x.png"),
    "create_call": ("Icon_CreatePhoneCall", "phonePlus_30.png", "phonePlus_30@2x.png"),
    "call_outgoing": ("Icon_CallOutgoing", None, "CallOutgoing@2x.png"),
}
COLORED_ASSETS = {
    "Icon_Premium_Settings": "premiumbadge_16@2x.png",
    "Icon_SettingsProfile": "ic_prof@2x.png",
    "Icon_SettingsNotifications": "ic_notifications@2x.png",
    "Icon_SettingsSecurity": "ic_privacy@2x.png",
    "Icon_AppearanceSettings": "ic_theme@2x.png",
    "Icon_SettingsFilters": "ic_filters@2x.png",
    "Icon_SettingsStorage": "ic_data@2x.png",
    "Icon_SettingsGeneral": "ic_general@2x.png",
    "Icon_PrivacySettings_ActiveSessions": "sessions@2x.png",
    "Icon_SettingsLanguage": "ic_lang@2x.png",
    "Icon_SettingsStickers": "ic_stickers@2x.png",
    "Icon_SettingsStories": "my_profile@2x.png",
    "Icon_SettingsBusiness": "business_24@2x (2).png",
    "Icon_Settings_GiftPremium": "Gift_24_substracted@2x.png",
    "Icon_Settings_Stars": "stars@2x.png",
    "Icon_SettingsFaq": "ic_faq@2x.png",
    "Icon_SettingsAskQuestion": "ic_ask@2x.png",
}
ANIMATIONS = ("tab_contacts", "tab_calls", "tab_chats", "tab_settings")


def alpha_mask(content: bytes):
    if content[12:16] == b"CgBI":
        header = None
        compressed = bytearray()
        offset = 8
        while offset < len(content):
            length = struct.unpack(">I", content[offset:offset + 4])[0]
            kind = content[offset + 4:offset + 8]
            data = content[offset + 8:offset + 8 + length]
            if kind == b"IHDR":
                header = data
            elif kind == b"IDAT":
                compressed.extend(data)
            offset += length + 12
        def chunk(kind, data):
            return (struct.pack(">I", len(data)) + kind + data
                    + struct.pack(">I", zlib.crc32(kind + data)))
        content = (content[:8] + chunk(b"IHDR", header)
                   + chunk(b"IDAT", zlib.compress(zlib.decompress(compressed, -15)))
                   + chunk(b"IEND", b""))
    return Image.open(BytesIO(content)).convert("RGBA").getchannel("A")


def write_if_changed(path: Path, content: bytes):
    if not path.is_file() or path.read_bytes() != content:
        path.write_bytes(content)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    revision = subprocess.check_output(
        ["git", "-C", str(args.donor), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != REVISION:
        raise ValueError(f"Expected donor {REVISION}, got {revision}")
    failed = False
    manifest = {}
    for target, (asset, single, double) in ASSETS.items():
        folder = Path("Telegram-Mac/Assets.xcassets") / (asset + ".imageset")
        source = {}
        for scale, name in ((1, single), (2, double)):
            if name is None:
                continue
            relative = (folder / name).as_posix()
            content = subprocess.check_output([
                "git", "-C", str(args.donor), "show", f"{REVISION}:{relative}"
            ])
            source[scale] = alpha_mask(content)
        if single is None:
            width, height = source[2].size
            source[1] = source[2].resize((width // 2, height // 2), Image.Resampling.LANCZOS)
        width, height = source[1].size
        source[3] = source[2].resize((width * 3, height * 3), Image.Resampling.LANCZOS)
        for scale, mask in source.items():
            suffix = "" if scale == 1 else f"@{scale}x"
            path = ROOT / f"Telegram/Resources/icons/macos/{target}{suffix}.png"
            expected = mask.convert("RGB")
            if args.check:
                with Image.open(path) as actual:
                    if actual.size != expected.size or actual.convert("RGB").tobytes() != expected.tobytes():
                        print(f"OUT OF SYNC: {path.relative_to(ROOT)}")
                        failed = True
            else:
                output = BytesIO()
                expected.save(output, format="PNG", optimize=True)
                write_if_changed(path, output.getvalue())
        manifest[target] = {
            "path": (folder / double).as_posix(),
            "blob": subprocess.check_output([
                "git", "-C", str(args.donor), "rev-parse", f"{REVISION}:{(folder / double).as_posix()}"
            ], text=True).strip(),
            "size": [width, height],
        }
        if target == "call_outgoing":
            manifest[target]["source_scale"] = 2
    for asset, filename in COLORED_ASSETS.items():
        relative = f"Telegram-Mac/Assets.xcassets/{asset}.imageset/{filename}"
        content = subprocess.check_output([
            "git", "-C", str(args.donor), "show", f"{REVISION}:{relative}"
        ])
        path = ROOT / f"Telegram/Resources/icons/macos/native-{asset}.png"
        if args.check:
            with Image.open(BytesIO(content)) as expected, Image.open(path) as actual:
                if actual.size != expected.size or actual.convert("RGBA").tobytes() != expected.convert("RGBA").tobytes():
                    print(f"OUT OF SYNC: {path.relative_to(ROOT)}")
                    failed = True
        else:
            write_if_changed(path, content)
        manifest[asset] = {
            "path": relative,
            "blob": subprocess.check_output([
                "git", "-C", str(args.donor), "rev-parse", f"{REVISION}:{relative}"
            ], text=True).strip(),
            "size": [dimension // 2 for dimension in Image.open(BytesIO(content)).size],
        }
    for name in ANIMATIONS:
        relative = f"Resources/tgs/{name}.tgs"
        content = subprocess.check_output([
            "git", "-C", str(args.donor), "show", f"{REVISION}:{relative}"
        ])
        animation = json.loads(gzip.decompress(content))
        path = ROOT / f"Telegram/Resources/animations/macos/{name}.tgs"
        if args.check:
            if not path.is_file() or path.read_bytes() != content:
                print(f"OUT OF SYNC: {path.relative_to(ROOT)}")
                failed = True
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            write_if_changed(path, content)
        manifest[name] = {
            "path": relative,
            "blob": subprocess.check_output([
                "git", "-C", str(args.donor), "rev-parse", f"{REVISION}:{relative}"
            ], text=True).strip(),
            "size": [animation["w"], animation["h"]],
            "frames": animation["op"] - animation["ip"],
            "fps": animation["fr"],
        }
    path = Path(__file__).with_name("assets.json")
    content = json.dumps(manifest, indent=2) + "\n"
    if args.check:
        if path.read_text(encoding="utf-8") != content:
            print("OUT OF SYNC: asset manifest")
            failed = True
    else:
        write_if_changed(path, content.replace("\n", "\r\n").encode("utf-8"))
    sources = (
        "# Native Telegram macOS assets\n\n"
        "Source: TelegramMessenger/telegram-macos, revision `" + REVISION + "`.\n"
        "The donor's GPL license applies to these assets.\n\n"
        "For the monochrome icons, the original 1x and 2x alpha channels are "
        "stored as RGB masks without resizing. The 3x masks are resampled from 2x "
        "with Lanczos. The outgoing call mask only exists at 2x in the donor: its 1x "
        "and 3x masks are resampled with Lanczos. PNG row order is preserved for "
        "rendering in the Qt canvas. Colored settings icons retain their original 2x RGBA pixels "
        "and render at 24 logical pixels. The Premium mask renders at 16 logical "
        "pixels inside the 24-pixel gradient tile defined by `generatePremiumIcon()` "
        "in the donor's `packages/TelegramTheme/Sources/TelegramTheme/Appearance.swift`. "
        "The four tab animations retain the original compressed TGS bytes and "
        "play in a 48-pixel canvas as defined by `TabAnimatedIconView` in "
        "`packages/ChatListUI/Sources/ChatListUI/TabBadgeItem.swift`.\n\n"
        "Regenerate or verify with `tools/macos_design/sync_assets.py --donor PATH` "
        "and the optional `--check` flag.\n\n"
    )
    for name, asset in manifest.items():
        url = f"https://github.com/TelegramMessenger/telegram-macos/blob/{REVISION}/{quote(asset['path'])}"
        sources += f"- [{name}]({url}) вЂ” blob `{asset['blob']}`.\n"
    path = ROOT / "Telegram/Resources/icons/macos/SOURCES.md"
    if args.check:
        if path.read_text(encoding="utf-8") != sources:
            print("OUT OF SYNC: icon source attribution")
            failed = True
    else:
        write_if_changed(path, sources.replace("\n", "\r\n").encode("utf-8"))
    print(f"{len(ASSETS)} masks at three scales, {len(COLORED_ASSETS)} original color icons, {len(ANIMATIONS)} animations: {'FAIL' if failed else 'OK'}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
