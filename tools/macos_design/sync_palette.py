import argparse
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = Path(__file__).with_name("donor.json")
REVISION = "273b7c4870b0ddf1bf3a56afb3e6937109e574ba"
SOURCE = "packages/ColorPalette/Sources/ColorPalette/ColorPalette.swift"
GROUPS = {
    "background": "windowBg boxBg contactsBg dialogsBg topBarBg historyComposeAreaBg historyReplyBg titleBg titleBgActive lightButtonBg menuBg",
    "text": "windowFg windowFgOver windowBoldFg windowBoldFgOver boxTextFg boxTitleFg contactsNameFg dialogsNameFg dialogsNameFgOver historyTextInFg historyComposeAreaFg",
    "grayText": "windowSubTextFg windowSubTextFgOver dialogsDateFg dialogsDateFgOver dialogsTextFg dialogsTextFgOver contactsStatusFg placeholderFg placeholderFgActive msgInDateFg msgInDateFgSelected",
    "grayForeground": "windowBgOver windowBgRipple dialogsBgOver",
    "grayBackground": "filterInputInactiveBg filterInputActiveBg searchedBarBg",
    "listBackground": "boxSearchBg",
    "grayIcon": "menuIconFg menuIconFgOver dialogsMenuIconFg dialogsMenuIconFgOver historyComposeIconFg emojiIconFg",
    "accent": "windowBgActive windowActiveTextFg activeButtonBg activeButtonBgOver activeLineFg lightButtonFg lightButtonFgOver filterInputBorderFg contactsStatusFgOnline dialogsTextFgService dialogsTextFgServiceOver dialogsSentIconFg dialogsSentIconFgOver historyComposeIconFgOver historySendIconFg historySendIconFgOver historyToDownFg historyReplyIconFg",
    "accentSelect": "dialogsBgActive dialogsUnreadFgActive",
    "border": "shadowFg windowShadowFgFallback msgInReplyBarColor msgInReplyBarSelColor",
    "badge": "dialogsUnreadBg dialogsUnreadBgOver",
    "badgeMuted": "dialogsUnreadBgMuted dialogsUnreadBgMutedOver",
    "redUI": "attentionButtonFg boxTextFgError dialogsDraftFg dialogsScamFg callArrowMissedFg",
    "greenUI": "boxTextFgGood dialogsOnlineBadgeFg callArrowFg",
    "bubbleBackground_incoming": "msgInBg",
    "bubbleBackground_outgoing": "msgOutBg",
    "bubbleBackgroundHighlight_incoming": "msgInBgSelected",
    "bubbleBackgroundHighlight_outgoing": "msgOutBgSelected",
    "bubbleBorder_incoming": "msgInShadow msgInShadowSelected",
    "bubbleBorder_outgoing": "msgOutShadow msgOutShadowSelected",
    "textBubble_outgoing": "historyTextOutFg historyTextOutFgSelected historyFileNameOutFg historyFileNameOutFgSelected",
    "grayTextBubble_outgoing": "msgOutDateFg msgOutDateFgSelected historyOutIconFg historyOutIconFgSelected historySendingOutIconFg mediaOutFg mediaOutFgSelected",
    "linkBubble_incoming": "historyLinkInFg historyLinkInFgSelected",
    "linkBubble_outgoing": "historyLinkOutFg historyLinkOutFgSelected msgFileThumbLinkOutFg msgFileThumbLinkOutFgSelected",
    "chatReplyTitleBubble_incoming": "msgInServiceFg msgInServiceFgSelected",
    "chatReplyTitleBubble_outgoing": "msgOutServiceFg msgOutServiceFgSelected msgOutReplyBarColor msgOutReplyBarSelColor",
    "monospacedCodeBubble_incoming": "msgInMonoFg msgInMonoFgSelected",
    "monospacedCodeBubble_outgoing": "msgOutMonoFg msgOutMonoFgSelected",
    "fileActivityBackgroundBubble_incoming": "msgFileInBg msgFileInBgOver msgFileInBgSelected",
    "fileActivityBackgroundBubble_outgoing": "msgFileOutBg msgFileOutBgSelected",
    "fileActivityForegroundBubble_incoming": "historyFileInIconFg historyFileInIconFgSelected historyFileInRadialFg historyFileInRadialFgSelected",
    "fileActivityForegroundBubble_outgoing": "historyFileOutIconFg historyFileOutIconFgSelected historyFileOutRadialFg historyFileOutRadialFgSelected",
    "waveformForegroundBubble_incoming": "msgWaveformInActive msgWaveformInActiveSelected",
    "waveformBackgroundBubble_incoming": "msgWaveformInInactive msgWaveformInInactiveSelected",
    "waveformForegroundBubble_outgoing": "msgWaveformOutActive msgWaveformOutActiveSelected",
    "waveformBackgroundBubble_outgoing": "msgWaveformOutInactive msgWaveformOutInactiveSelected",
    "chatDateText": "msgServiceFg",
    "chatDateActive": "msgServiceBg",
}
GROUPS["background"] += " dialogsUnreadFg dialogsUnreadFgOver"
GROUPS["grayText"] += " historySendingInIconFg historyComposeAreaFgService"
GROUPS["grayIcon"] += " cancelIconFg cancelIconFgOver"
GROUPS["background"] += " mainMenuBg mainMenuCoverBg historyPinnedBg emojiPanCategories emojiPanHeaderBg tooltipBg"
GROUPS["text"] += " titleFg titleFgActive tooltipFg menuSubmenuArrowFg sideBarTextFg"
GROUPS["grayText"] += " boxTitleAdditionalFg dialogsSendingIconFg dialogsSendingIconFgOver sideBarIconFg"
GROUPS["grayBackground"] += " menuBgOver lightButtonBgOver sliderBgInactive"
GROUPS["grayForeground"] += " menuBgRipple lightButtonBgRipple activeButtonBgRipple"
GROUPS["border"] += " inputBorderFg tooltipBorderFg menuSeparatorFg"
GROUPS["accent"] += " mainMenuCloudBg sideBarTextFgActive sideBarIconFgActive historyUnreadBarFg"
GROUPS["accentSelect"] += " sideBarBgActive"
GROUPS["listBackground"] += " sideBarBg"
GROUPS["badge"] += " sideBarBadgeBg sideBarBadgeBgActive"
GROUPS["badgeMuted"] += " sideBarBadgeBgMuted sideBarBadgeBgMutedActive"
GROUPS["redUI"] += " activeLineFgError attentionButtonFgOver"
GROUPS["greenUI"] += " historyCallArrowInFg historyCallArrowInFgSelected"
GROUPS["grayTextBubble_incoming"] = "mediaInFg mediaInFgSelected"
GROUPS["textBubble_incoming"] = "historyFileNameInFg historyFileNameInFgSelected"
GROUPS["grayBackground"] += " historyUnreadBarBg"
for index, name in enumerate(("Red", "Green", "Orange", "Blue", "Violet", "Pink", "Cyan"), 1):
    GROUPS[f"peerAvatar{name}Top"] = f"historyPeer{index}UserpicBg"
    GROUPS[f"peerAvatar{name}Bottom"] = f"historyPeer{index}UserpicBg2"


def import_donor(donor):
    revision = subprocess.check_output(
        ["git", "-C", str(donor), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != REVISION:
        raise ValueError(f"Expected donor {REVISION}, got {revision}")
    swift = subprocess.check_output(
        ["git", "-C", str(donor), "show", f"{REVISION}:{SOURCE}"],
        text=True, encoding="utf-8"
    )
    palettes = {}
    for theme, name in (("light", "whitePalette"), ("dark", "darkPalette")):
        block = swift.split(f"public let {name} = ColorPalette(", 1)[1]
        block = block.split("\n)", 1)[0]
        colors = {}
        for match in re.finditer(r"^\s*(\w+)\s*:\s*(\[?NSColor\([^\n]+)", block, re.M):
            values = []
            for color in re.finditer(r"NSColor\(0x([0-9a-fA-F]{6})(?:,\s*([0-9.]+))?\)", match[2]):
                value = "#" + color[1].lower()
                if color[2] is not None:
                    value += f"{int(float(color[2]) * 255 + 0.5):02x}"
                values.append(value)
            if values:
                colors[match[1]] = values if match[2].startswith("[") else values[0]
        palettes[theme] = colors
    snapshot = {
        "repository": "https://github.com/TelegramMessenger/telegram-macos",
        "revision": revision,
        "source": SOURCE,
        "palettes": palettes,
    }
    return json.dumps(snapshot, indent=2) + "\n"


def mapped_colors(colors):
    result = {}
    for source, targets in GROUPS.items():
        value = colors[source]
        if isinstance(value, list):
            value = value[0]
        for target in targets.split():
            result[target] = value
    for target in (
        "dialogsNameFgActive", "dialogsDateFgActive", "dialogsTextFgActive",
        "dialogsTextFgServiceActive", "dialogsUnreadBgActive", "dialogsUnreadBgMutedActive",
    ):
        result[target] = "#ffffff"
    result["windowBgOver"] = colors["grayBackground"]
    result["windowBgRipple"] = colors["grayForeground"]
    if colors["background"] == "#ffffff":
        result["dialogsBgOver"] = colors["grayBackground"]
    return result


def sync(check):
    snapshot = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
    declarations = set(re.findall(r"^(\w+):", (ROOT / "Telegram/lib_ui/ui/colors.palette").read_text(encoding="utf-8"), re.M))
    failed = False
    gradients = []
    for theme, name in (("light", "Light"), ("dark", "Dark")):
        values = snapshot["palettes"][theme]["bubbleBackground_outgoing"]
        if len(values) != 2:
            raise ValueError("Expected exactly two outgoing gradient stops")
        stops = ", ".join("0x" + value[1:] for value in values)
        gradients.append(
            f"inline constexpr auto kMac{name}OutgoingGradient = "
            f"std::array<std::uint32_t, 2>{{\n\t{stops},\n}};"
        )
    header = (
        "#pragma once\n\n#include <array>\n#include <cstdint>\n#include <vector>\n"
        "#include <QtGui/QColor>\n\nnamespace Ui {\n\n"
        + "\n".join(gradients)
        + "\n\n[[nodiscard]] inline std::vector<QColor> MacOutgoingGradient(\n"
        "\t\tconst QColor &color) {\n"
        "\tfor (const auto &gradient : {\n\t\tkMacLightOutgoingGradient,\n\t\tkMacDarkOutgoingGradient,\n\t}) {\n"
        "\t\tif (color == QColor::fromRgb(gradient.front())) {\n"
        "\t\t\treturn {\n\t\t\t\tQColor::fromRgb(gradient.front()),\n"
        "\t\t\t\tQColor::fromRgb(gradient.back()),\n\t\t\t};\n"
        "\t\t}\n\t}\n\treturn {};\n}\n\n}\n"
    )
    header_path = ROOT / "Telegram/SourceFiles/ui/chat/chat_theme_macos.h"
    if not header_path.exists() or header_path.read_text(encoding="utf-8") != header:
        if check:
            print("OUT OF SYNC: outgoing gradient header")
            failed = True
        else:
            header_path.write_bytes(header.replace("\n", "\r\n").encode("utf-8"))
    for theme, colors in snapshot["palettes"].items():
        path = ROOT / f"Telegram/Resources/macos-{theme}.tdesktop-theme"
        original = path.read_text(encoding="utf-8")
        content = original
        mapped = mapped_colors(colors)
        for target, value in mapped.items():
            if target not in declarations:
                raise ValueError(f"Unknown palette field: {target}")
            pattern = rf"(?m)^{target}:\s*[^;]+;"
            replacement = f"{target}: {value};"
            if re.search(pattern, content):
                content = re.sub(pattern, replacement, content)
            else:
                content += replacement + "\n"
        if original != content:
            if check:
                print(f"OUT OF SYNC: {path.relative_to(ROOT)}")
                failed = True
            else:
                path.write_bytes(content.replace("\n", "\r\n").encode("utf-8"))
        print(f"{theme}: {len(mapped)} colors matched to donor {snapshot['revision'][:12]}")
    return int(failed)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--donor", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.donor:
        content = import_donor(args.donor)
        if args.check:
            if SNAPSHOT.read_text(encoding="utf-8") != content:
                print("OUT OF SYNC: donor palette snapshot")
                return 1
        else:
            SNAPSHOT.write_bytes(content.replace("\n", "\r\n").encode("utf-8"))
    return sync(args.check)


if __name__ == "__main__":
    raise SystemExit(main())
