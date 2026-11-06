"""Mapped DNS: fake-IP resolver whose addresses are sent as domain names."""
import ipaddress

from hevtest import client
from hevtest.case import TunnelTestCase

DNS = "198.18.0.2"
NET = ipaddress.ip_network("100.64.0.0/10")
MAPDNS = {"address": DNS, "port": 53, "network": "100.64.0.0",
          "netmask": "255.192.0.0", "cache-size": 100}


class MapDnsTest(TunnelTestCase):

    def setUp(self):
        super().setUp()
        self.tunnel(mapdns=MAPDNS)

    def a(self, name):
        r = client.dns_query(DNS, name)
        self.assertIsNotNone(r, "no DNS reply for " + name)
        return r

    def test_answers_in_mapped_network(self):
        r = self.a("example.test")
        self.assertEqual(len(r), 1)
        self.assertIn(ipaddress.ip_address(r[0]), NET)

    def test_stable_mapping(self):
        self.assertEqual(self.a("stable.test"), self.a("stable.test"))

    def test_distinct_names_distinct_addresses(self):
        names = ["host%d.test" % i for i in range(20)]
        addrs = {self.a(n)[0] for n in names}
        self.assertEqual(len(addrs), len(names))

    def test_aaaa_has_no_answer(self):
        r = client.dns_query(DNS, "example.test", qtype=28)
        self.assertEqual(r, [])

    def test_connect_by_mapped_address(self):
        # "localhost" is resolved by the SOCKS5 server side, where the
        # target services listen on the loopback as well.
        ip = self.a("localhost")[0]
        self.assertTrue(client.tcp_echo(ip, self.bed.P_ECHO, 20000))

    def test_cache_eviction(self):
        # more names than cache-size must keep working
        for i in range(250):
            self.a("evict%d.test" % i)
        ip = self.a("localhost")[0]
        self.assertTrue(client.tcp_echo(ip, self.bed.P_ECHO, 1000))

    def test_garbage_query_does_not_crash(self):
        for junk in (b"", b"\x00" * 3, b"\xff" * 64, b"\x12\x34\x01\x00" +
                     b"\xff" * 40):
            client.udp_echo(DNS, 53, junk, 0.3, 1)
        self.assertTrue(self.a("after-junk.test"))
