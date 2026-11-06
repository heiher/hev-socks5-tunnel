"""SOCKS5 username/password authentication and pipeline handshake."""
import os

from hevtest import client
from hevtest.case import TunnelTestCase

PORT = 1081


class AuthTest(TunnelTestCase):

    def setUp(self):
        super().setUp()
        srv = self.bed.start_socks("alice", "s3cret", port=PORT)
        self.addCleanup(srv.stop)

    def creds(self, user, pw, **extra):
        s = {"port": PORT, "username": user, "password": pw}
        s.update(extra)
        return s

    def test_valid_credentials(self):
        self.tunnel(socks5=self.creds("alice", "s3cret"))
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 5000))
        self.assertTrue(client.tcp_echo(self.t6, self.bed.P_ECHO, 5000))
        p = os.urandom(300)
        self.assertEqual(client.udp_echo(self.t4, self.bed.P_ECHO, p), p)

    def test_valid_credentials_udp_in_tcp(self):
        self.tunnel(socks5=self.creds("alice", "s3cret", udp="tcp"))
        p = os.urandom(300)
        self.assertEqual(client.udp_echo(self.t4, self.bed.P_ECHO, p), p)

    def test_wrong_password(self):
        self.tunnel(socks5=self.creds("alice", "nope"))
        self.assertTrue(client.tcp_blocked(self.t4, self.bed.P_ECHO, 5.0))

    def test_wrong_user(self):
        self.tunnel(socks5=self.creds("mallory", "s3cret"))
        self.assertTrue(client.tcp_blocked(self.t4, self.bed.P_ECHO, 5.0))

    def test_missing_credentials(self):
        self.tunnel(socks5={"port": PORT})
        self.assertTrue(client.tcp_blocked(self.t4, self.bed.P_ECHO, 5.0))

    def test_pipeline_with_auth(self):
        self.tunnel(socks5=self.creds("alice", "s3cret", pipeline=True))
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 5000))


class PipelineTest(TunnelTestCase):

    def test_pipeline_no_auth(self):
        self.tunnel(socks5={"pipeline": True})
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 5000))
        self.assertTrue(client.tcp_echo(self.t6, self.bed.P_ECHO, 5000))
        p = os.urandom(200)
        self.assertEqual(client.udp_echo(self.t4, self.bed.P_ECHO, p), p)
