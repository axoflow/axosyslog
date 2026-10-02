`cloud-auth()`: Added `auth_url()` to `azure(monitor())`, which names the login host the access token is
requested from.

The token endpoint was fixed at `https://login.microsoftonline.com`, so only the public cloud could be reached.
The tenant and `/oauth2/v2.0/token` are still appended, and the default is unchanged.
