char *gfarm_gsi_cred_config_convert_to_name_and_type(
	enum gfarm_auth_cred_type, char *, char *, char *,
	char **, gss_OID *, int *);

void gfarm_gsi_set_delegated_cred(gss_cred_id_t);
gss_cred_id_t gfarm_gsi_get_delegated_cred(void);
