import copy
import datetime
import hashlib
import unittest
from apple_signing import development_signing


class SigningTests(unittest.TestCase):
    def setUp(self):
        self.now = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
        self.profile = {
            'ExpirationDate': datetime.datetime(2026, 2, 1),
            'DeveloperCertificates': [b'test certificate'],
            'Entitlements': {'application-identifier': 'PREFIX.dev.example.*',
                             'com.apple.developer.team-identifier': 'TEAM',
                             'get-task-allow': True},
        }
        self.identity = hashlib.sha1(b'test certificate').hexdigest().upper()

    def sign(self, profile=None, bundle='dev.example.relay', identities=None):
        return development_signing(profile or self.profile, bundle,
                                   self.identity if identities is None else identities, self.now)

    def test_scoped_wildcard_and_distinct_app_prefix(self):
        identity, entitlements = self.sign()
        self.assertEqual(identity, self.identity)
        self.assertEqual(entitlements['application-identifier'], 'PREFIX.dev.example.relay')
        self.assertEqual(entitlements['com.apple.developer.team-identifier'], 'TEAM')
        with self.assertRaisesRegex(ValueError, 'does not allow'):
            self.sign(bundle='dev.other.relay')

    def test_specific_profile(self):
        self.profile['Entitlements']['application-identifier'] = 'PREFIX.dev.example.relay'
        self.sign()
        with self.assertRaisesRegex(ValueError, 'does not allow'):
            self.sign(bundle='dev.example.relay.extra')

    def test_expiry_distribution_and_unavailable_identity(self):
        p = copy.deepcopy(self.profile)
        p['ExpirationDate'] = self.now
        with self.assertRaisesRegex(ValueError, 'expired'): self.sign(p)
        p = copy.deepcopy(self.profile)
        p['Entitlements']['get-task-allow'] = False
        with self.assertRaisesRegex(ValueError, 'development profile'): self.sign(p)
        with self.assertRaisesRegex(ValueError, 'No valid signing identity'): self.sign(identities='')
        with self.assertRaisesRegex(ValueError, 'bundle-id'): self.sign(bundle='bad/id')
