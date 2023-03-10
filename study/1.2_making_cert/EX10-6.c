#include <stdio.h>
#include <stdlib.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/pem.h>

void handle_error (const char *file, int lineno, const char *msg)
{
  fprintf (stderr, "** %s:%i %s\n", file, lineno, msg);
  ERR_print_errors_fp (stderr);
  exit (-1);
}

#define int_error(msg) handle_error(__FILE__, __LINE__, msg)

/* these are defintions to make the example simpler */
#define CA_FILE "rootCA.pem"
#define CA_KEY "CAkey.pem"
#define REQ_FILE "newreq.pem"
#define CERT_FILE "newcert.pem"
#define DAYS_TILL_EXPIRE 365
#define EXPIRE_SECS (60*60*24*DAYS_TILL_EXPIRE)

#define EXT_COUNT 5

struct entry
{
  char *key;
  char *value;
};

struct entry ext_ent[EXT_COUNT] = {
  {"basicConstraints", "CA:FALSE"},
  {"nsComment", "\"OpenSSL Generated Certificate\""},
  {"subjectKeyIdentifier", "hash"},
  {"authorityKeyIdentifier", "keyid,issuer:always"},
  {"keyUsage", "nonRepudiation,digitalSignature,keyEncipherment"}
};

int main (int argc, char *argv[])
{
  int i, subjAltName_pos;
  long serial = 1;
  EVP_PKEY *pkey, *CApkey;
  const EVP_MD *digest;
  X509 *cert, *CAcert;
  X509_REQ *req;
  X509_NAME *name;
  X509V3_CTX ctx;
  X509_EXTENSION *subjAltName;
  STACK_OF (X509_EXTENSION) * req_exts;
  FILE *fp;
  BIO *out;

  OpenSSL_add_all_algorithms ();
  ERR_load_crypto_strings ();
  //seed_prng ();

/* open stdout */
  if (!(out = BIO_new_fp (stdout, BIO_NOCLOSE)))
    int_error ("Error creating stdout BIO");

/* read in the request */
  if (!(fp = fopen (REQ_FILE, "r")))
    int_error ("Error reading request file");
  if (!(req = PEM_read_X509_REQ (fp, NULL, NULL, NULL)))
    int_error ("Error reading request in file");
  fclose (fp);

/* verify signature on the request */
  if (!(pkey = X509_REQ_get_pubkey (req)))
    int_error ("Error getting public key from request");
  if (X509_REQ_verify (req, pkey) != 1)
    int_error ("Error verifying signature on certificate");

/* read in the CA certificate */
  if (!(fp = fopen (CA_FILE, "r")))
    int_error ("Error reading CA certificate file");
  if (!(CAcert = PEM_read_X509 (fp, NULL, NULL, NULL)))
    int_error ("Error reading CA certificate in file");
  fclose (fp);

/* read in the CA private key */
  if (!(fp = fopen (CA_KEY, "r")))
    int_error ("Error reading CA private key file");
  if (!(CApkey = PEM_read_PrivateKey (fp, NULL, NULL, "123456")))
    int_error ("Error reading CA private key in file");
  fclose (fp);

/* print out the subject name and subject alt name extension */
  if (!(name = X509_REQ_get_subject_name (req)))
    int_error ("Error getting subject name from request");
  X509_NAME_print (out, name, 0);
  fputc ('\n', stdout);
  if (!(req_exts = X509_REQ_get_extensions (req)))
    int_error ("Error getting the request's extensions");
  subjAltName_pos = X509v3_get_ext_by_NID (req_exts,
					   OBJ_sn2nid ("subjectAltName"), -1);
  subjAltName = X509v3_get_ext (req_exts, subjAltName_pos);
  X509V3_EXT_print (out, subjAltName, 0, 0);
  fputc ('\n', stdout);

/* WE SHOULD NOW ASK WHETHER TO CONTINUE OR NOT */

/* create new certificate */
  if (!(cert = X509_new ()))
    int_error ("Error creating X509 object");

/* set version number for the certificate (X509v3) and the serial number */
  if (X509_set_version (cert, 2L) != 1)
    int_error ("Error settin certificate version");
  ASN1_INTEGER_set (X509_get_serialNumber (cert), serial++);

/* set issuer and subject name of the cert from the req and the CA */
  if (!(name = X509_REQ_get_subject_name (req)))
    int_error ("Error getting subject name from request");
  if (X509_set_subject_name (cert, name) != 1)
    int_error ("Error setting subject name of certificate");
  if (!(name = X509_get_subject_name (CAcert)))
    int_error ("Error getting subject name from CA certificate");
  if (X509_set_issuer_name (cert, name) != 1)
    int_error ("Error setting issuer name of certificate");

/* set public key in the certificate */
  if (X509_set_pubkey (cert, pkey) != 1)
    int_error ("Error setting public key of the certificate");

/* set duration for the certificate */
  if (!(X509_gmtime_adj (X509_get_notBefore (cert), 0)))
    int_error ("Error setting beginning time of the certificate");
  if (!(X509_gmtime_adj (X509_get_notAfter (cert), EXPIRE_SECS)))
    int_error ("Error setting ending time of the certificate");

/* add x509v3 extensions as specified */
  X509V3_set_ctx (&ctx, CAcert, cert, NULL, NULL, 0);
  for (i = 0; i < EXT_COUNT; i++)
    {
      X509_EXTENSION *ext;
      if (!(ext = X509V3_EXT_conf (NULL, &ctx,
				   ext_ent[i].key, ext_ent[i].value)))
	{
	  fprintf (stderr, "Error on \"%s = %s\"\n",
		   ext_ent[i].key, ext_ent[i].value);
	  int_error ("Error creating X509 extension object");
	}
      if (!X509_add_ext (cert, ext, -1))
	{
	  fprintf (stderr, "Error on \"%s = %s\"\n",
		   ext_ent[i].key, ext_ent[i].value);
	  int_error ("Error adding X509 extension to certificate");
	}
      X509_EXTENSION_free (ext);
    }

/* add the subjectAltName in the request to the cert */
  if (!X509_add_ext (cert, subjAltName, -1))
    int_error ("Error adding subjectAltName to certificate");

/* sign the certificate with the CA private key */
    digest = EVP_sha1 ();
    //int_error ("Error checking CA private key for a valid digest");
  if (!(X509_sign (cert, CApkey, digest)))
    int_error ("Error signing certificate");

/* write the completed certificate */
  if (!(fp = fopen (CERT_FILE, "w")))
    int_error ("Error writing to certificate file");
  if (PEM_write_X509 (fp, cert) != 1)
    int_error ("Error while writing certificate");
  fclose (fp);

  return 0;
}
