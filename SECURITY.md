# Security Policy

## Supported Versions

Security updates are provided for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| Latest  | :white_check_mark: |
| Nightly | :white_check_mark: |
| Older   | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability in MeshCore, please report it privately:

1. **DO NOT** create a public GitHub issue
2. Email the maintainer directly (see GitHub profile)
3. Or use GitHub's private vulnerability reporting feature

Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix if you have one

## Response Timeline

- Acknowledgment within 48 hours
- Initial assessment within 1 week
- Fix development depends on severity
- Public disclosure after fix is available

## Security Considerations

### Encryption
MeshCore supports encrypted communication in Simple Secure Chat example. Encryption keys must be managed securely by the application.

### Default Credentials
- Default admin password is documented as 123456
- **Change immediately** on production deployments
- Never commit credentials to git repository

### Network Security
- LoRa mesh is a radio network (no physical access control)
- Anyone in range can receive packets
- Use encryption for sensitive data
- Implement authentication at application layer

### Physical Security
- Devices in field should be physically secured
- Serial console provides full access
- Consider disabling debug features in production

## Best Practices

### For Developers
- Never commit API keys, passwords, or tokens
- Use environment variables for sensitive config
- Review code for information disclosure
- Follow secure coding guidelines

### For Users
- Change default passwords
- Use encryption features
- Keep firmware updated
- Secure physical access to devices
- Monitor for unusual network activity

## CI Security Scanning

MeshCore includes automated security scanning:
- Credential leak detection
- Dependency vulnerability scanning
- Code security analysis

These run automatically on pull requests.
