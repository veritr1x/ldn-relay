"""Build a locally signed Mac Catalyst companion from the shared UIKit client."""
from pathlib import Path
import plistlib
import re
import subprocess

root = Path(__file__).resolve().parent
relay = root.parent
version = re.search(r'#define RELAY_VERSION "([^"]+)"', (relay / 'common/relay_protocol.h').read_text()).group(1)
output = relay.parent / 'build' / ('relay-mac-v' + version)
app = output / 'LDN Relay Mac.app'
contents = app / 'Contents'
binary = contents / 'MacOS' / 'LDNRelayMac'
binary.parent.mkdir(parents=True, exist_ok=True)
(contents / 'Resources').mkdir(exist_ok=True)
info = {
    'LDNRelaySendWindow': 3, 'LDNRelayNotificationPaceMs': 15,
    'LDNRelayAckDelayMs': 0, 'LDNRelayDiagnostics': False,
    'CFBundleIdentifier': 'dev.local.ldn-relay.mac',
    'CFBundleExecutable': 'LDNRelayMac',
    'CFBundleName': 'LDN Relay Mac',
    'CFBundleDisplayName': 'LDN Relay Mac',
    'CFBundlePackageType': 'APPL',
    'CFBundleVersion': '1',
    'CFBundleShortVersionString': version,
    'CFBundleInfoDictionaryVersion': '6.0',
    'CFBundleSupportedPlatforms': ['MacOSX'],
    'LSMinimumSystemVersion': '14.0',
    'UIDeviceFamily': [2],
    'UIApplicationSupportsIndirectInputEvents': True,
    'UIApplicationSceneManifest': {
        'UIApplicationSupportsMultipleScenes': False,
        'UISceneConfigurations': {'UIWindowSceneSessionRoleApplication': [{
            'UISceneConfigurationName': 'Relay',
            'UISceneDelegateClassName': 'SceneDelegate',
        }]},
    },
    'NSBluetoothAlwaysUsageDescription': 'Relay local multiplayer packets through your modified Switch over Bluetooth.',
}
(contents / 'Info.plist').write_bytes(plistlib.dumps(info))
sdk = Path(subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip())
support = sdk / 'System' / 'iOSSupport'
frameworks = support / 'System' / 'Library' / 'Frameworks'
subprocess.run([
    'xcrun', '--sdk', 'macosx', 'clang', '-target', 'arm64-apple-ios17.0-macabi',
    '-isysroot', str(sdk), '-isystem', str(support / 'usr' / 'include'),
    '-iframework', str(frameworks), '-F', str(frameworks),
    '-L', str(support / 'usr' / 'lib'),
    '-Wl,-rpath,/System/iOSSupport/System/Library/Frameworks',
    '-fobjc-arc', '-fmodules', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-Wno-deprecated-declarations',
    '-framework', 'UIKit', '-framework', 'Foundation', '-framework', 'CoreBluetooth', '-framework', 'Security',
    '-I' + str(relay / 'common'), str(relay / 'ios' / 'main.m'),
    str(relay / 'common' / 'relay_codec.c'), str(relay / 'common' / 'relay_stream.c'), str(relay / 'common' / 'LRTransport.m'), str(relay / 'common' / 'LRLog.m'), '-o', str(binary),
], check=True)
subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
print('Built and signature verified:', app)
