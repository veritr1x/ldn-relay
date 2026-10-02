"""Build the ordinarily signed BLE companion using a supplied development profile."""
from pathlib import Path
import argparse
import plistlib
import re
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from apple_signing import development_signing

parser = argparse.ArgumentParser()
parser.add_argument('--profile', type=Path, required=True)
parser.add_argument('--bundle-id', default='dev.local.ldn-relay')
parser.add_argument('--diagnostics', action='store_true', help='Show benchmark controls (off in normal builds)')
parser.add_argument('--ble-window', type=int, default=3, choices=range(1,9))
parser.add_argument('--auto-benchmark', type=int, default=0, choices=[0,10,62,93])
parser.add_argument('--ble-pace', type=int, default=15, choices=range(0,101))
parser.add_argument('--ack-delay', type=int, default=0, choices=range(0,21))
parser.add_argument('--ldn-test', action='store_true')
parser.add_argument('--ldn-sweep', action='store_true')
parser.add_argument('--ldn-oneway', action='store_true')
parser.add_argument('--simultaneous', action='store_true')
parser.add_argument('--oneway', action='store_true')
parser.add_argument('--auto-sweep', action='store_true')
parser.add_argument('--duplex', action='store_true')
parser.add_argument('--compact-loopback', action='store_true')
parser.add_argument('--fault-notification-after-ms', type=int, default=0)
parser.add_argument('--fault-notification-duration-ms', type=int, default=0)
parser.add_argument('--reconnect-cycles', type=int, default=0, choices=range(21))
parser.add_argument('--soak-minutes', type=int, default=0, choices=range(61))
parser.add_argument('--output', type=Path)
args = parser.parse_args()
assert args.fault_notification_after_ms >= 0 and args.fault_notification_duration_ms >= 0
root = Path(__file__).resolve().parent
version = re.search(r'#define RELAY_VERSION "([^"]+)"', (root.parent / 'common/relay_protocol.h').read_text()).group(1)
output = args.output or root.parents[1] / 'build' / ('relay-ios-v' + version)
app = output / 'LDNRelay.app'
app.mkdir(parents=True, exist_ok=True)
profile = plistlib.loads(subprocess.check_output(['security', 'cms', '-D', '-i', str(args.profile)], stderr=subprocess.DEVNULL))
bundle_id = args.bundle_id
identities = subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
try:
    identity, entitlements = development_signing(profile, bundle_id, identities)
except ValueError as error:
    parser.error(str(error))
diagnostics = args.diagnostics or any((args.auto_benchmark, args.ldn_test, args.ldn_sweep,
    args.ldn_oneway, args.simultaneous, args.oneway, args.auto_sweep, args.duplex,
    args.compact_loopback, args.fault_notification_after_ms, args.fault_notification_duration_ms,
    args.reconnect_cycles, args.soak_minutes))
info = {
    'CFBundleIdentifier': bundle_id, 'CFBundleExecutable': 'LDNRelay',
    'CFBundleName': 'LDNRelay', 'CFBundleDisplayName': 'LDN Relay',
    'CFBundlePackageType': 'APPL', 'CFBundleVersion': '2',
    'CFBundleShortVersionString': version, 'CFBundleInfoDictionaryVersion': '6.0',
    'CFBundleSupportedPlatforms': ['iPhoneOS'], 'LSRequiresIPhoneOS': True,
    'MinimumOSVersion': '17.0', 'UIDeviceFamily': [1, 2], 'UILaunchScreen': {},
    'UIApplicationSceneManifest': {
        'UIApplicationSupportsMultipleScenes': False,
        'UISceneConfigurations': {'UIWindowSceneSessionRoleApplication': [{
            'UISceneConfigurationName': 'Relay',
            'UISceneDelegateClassName': 'SceneDelegate',
        }]},
    },
    'LDNRelayDiagnostics': diagnostics,
    'LDNRelayLDNTest': args.ldn_test or args.ldn_oneway or args.ldn_sweep,
    'LDNRelayLDNSweep': args.ldn_sweep,
    'LDNRelayLDNOneWay': args.ldn_oneway,
    'LDNRelaySimultaneous': args.simultaneous or args.ldn_test,
    'LDNRelayOneWay': args.oneway or args.simultaneous,
    'LDNRelayAutoSweep': args.auto_sweep,
    'LDNRelaySendWindow': args.ble_window,
    'LDNRelayAckDelayMs': args.ack_delay,
    'LDNRelayNotificationPaceMs': args.ble_pace,
    'LDNRelayAutoBenchmarkRate': args.auto_benchmark,
    'LDNRelayReconnectCycles': args.reconnect_cycles,
    'LDNRelaySoakMinutes': args.soak_minutes,
    'NSBluetoothAlwaysUsageDescription': 'Relay local multiplayer packets through your modified Switch over Bluetooth.',
}
if args.fault_notification_after_ms: info['LDNRelayFaultNotificationAfterMs'] = args.fault_notification_after_ms
if args.fault_notification_duration_ms: info['LDNRelayFaultNotificationDurationMs'] = args.fault_notification_duration_ms
if args.duplex: info['LDNRelayDuplex'] = True
if args.compact_loopback: info['LDNRelayCompactLoopback'] = True
(app / 'Info.plist').write_bytes(plistlib.dumps(info))
(output / 'entitlements.plist').write_bytes(plistlib.dumps(entitlements))
shutil.copy2(args.profile, app / 'embedded.mobileprovision')
sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
subprocess.run(['xcrun', '--sdk', 'iphoneos', 'clang', '-target', 'arm64-apple-ios17.0',
    '-isysroot', sdk, '-fobjc-arc', '-fmodules', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-Wno-deprecated-declarations', '-framework', 'UIKit',
    '-framework', 'Foundation', '-framework', 'CoreBluetooth', '-framework', 'Security',
    '-I' + str(root.parent / 'common'), str(root / 'main.m'),
    str(root.parent / 'common' / 'relay_codec.c'), str(root.parent / 'common' / 'relay_stream.c'), str(root.parent / 'common' / 'LRTransport.m'), str(root.parent / 'common' / 'LRLog.m'), '-o', str(app / 'LDNRelay')], check=True)
subprocess.run(['codesign', '--force', '--sign', identity, '--entitlements',
    str(output / 'entitlements.plist'), str(app)], check=True, capture_output=True)
subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
print('Built and signature verified:', app)
