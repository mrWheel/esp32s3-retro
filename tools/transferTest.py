import argparse
import hashlib
import http.client
import os
import socket
import urllib.parse


def transferTest():
  parser = argparse.ArgumentParser(description="Hardware HTTP acceptance test. Creates and deletes unique files in exchange/common.")
  parser.add_argument("url", help="The full URL printed on the USB terminal, including its token fragment")
  arguments = parser.parse_args()
  address = urllib.parse.urlsplit(arguments.url)
  if address.scheme != "http" or not address.hostname or len(address.fragment) != 32:
    parser.error("Use the full http://device:8080/#token URL")
  cookie = ""
  token = address.fragment
  prefix = "hostTest-" + os.urandom(6).hex()
  createdPaths = []

  def request(method, path, data=None, authenticated=True, extraHeaders=None):
    connection = http.client.HTTPConnection(address.hostname, address.port or 8080, timeout=30)
    headers = {}
    if authenticated:
      headers = {"Cookie": cookie, "X-Retro-Token": token}
    if extraHeaders:
      headers.update(extraHeaders)
    connection.request(method, path, body=data, headers=headers)
    response = connection.getresponse()
    status = response.status
    responseHeaders = dict(response.getheaders())
    content = response.read()
    connection.close()
    return status, responseHeaders, content

  def fileUrl(path):
    return "/api/file?path=" + urllib.parse.quote(path, safe="")

  status, headers, content = request("POST", "/api/session", b"")
  assert status == 200, (status, content)
  cookie = headers["Set-Cookie"].split(";", 1)[0]
  try:
    assert request("GET", "/api/list?path=common", authenticated=False)[0] == 403
    assert request("GET", "/api/list?path=common")[0] == 200
    for suffix, data in [("zero.bin", b""), ("space name.bin", bytes(range(256)) * 64), ("large.bin", os.urandom(8 * 1024 * 1024))]:
      path = "common/" + prefix + "-" + suffix
      status, headers, content = request("PUT", fileUrl(path), data)
      assert status == 201, (status, content)
      createdPaths.append(path)
      assert request("PUT", fileUrl(path), b"must not overwrite")[0] == 409
      status, headers, downloaded = request("GET", fileUrl(path))
      assert status == 200
      assert hashlib.sha256(data).digest() == hashlib.sha256(downloaded).digest()
      print("PASS SHA-256 round trip:", suffix, len(data), "bytes")
    for attack in ["../images/cpm/x", "%2e%2e/images/x", "%252e%252e/x", "/etc/passwd", "common/%00x", "common/%5c../x", "common/x%0dy"]:
      assert request("GET", "/api/file?path=" + attack)[0] == 400
      assert request("DELETE", "/api/file?path=" + attack)[0] == 400
    interruptedPath = "common/" + prefix + "-interrupted.bin"
    connection = socket.create_connection((address.hostname, address.port or 8080), timeout=10)
    head = "PUT " + fileUrl(interruptedPath) + " HTTP/1.1\r\nHost: " + address.netloc + "\r\nCookie: " + cookie + "\r\nX-Retro-Token: " + token + "\r\nContent-Length: 1048576\r\n\r\n"
    connection.sendall(head.encode("ascii") + b"partial")
    connection.close()
    assert request("GET", fileUrl(interruptedPath))[0] == 404
    print("PASS: authentication, overwrite refusal, traversal, interrupted upload")
  finally:
    for path in createdPaths:
      status, headers, content = request("DELETE", fileUrl(path))
      assert status == 200, (status, content)
  print("PASS: test files deleted. Run manual ENTER, provisioning and SD tests next.")


if __name__ == "__main__":
  transferTest()
