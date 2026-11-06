"""Local ICMP echo replies."""
import shutil

from hevtest.case import TunnelTestCase


class IcmpTest(TunnelTestCase):

    def setUp(self):
        super().setUp()
        self.require(shutil.which("ping"), "ping is not installed")

    def test_reply_ipv4(self):
        self.tunnel(tunnel={"icmp": "reply"})
        self.assertTrue(self.bed.ping(self.t4))

    def test_off(self):
        self.tunnel(tunnel={"icmp": "off"})
        self.assertFalse(self.bed.ping(self.t4, timeout=1))
