/**
 *
 *  foaf.c
 *
 *  FOAF (Friend Of A Friend) RDF generation functions for mod_virgule.
 *  Props to everyone on the foaf-dev mailing list who provided feedback
 *  and suggestions.
 *
 *  Copyright (C) 2007 by R. Steven Rainwater
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **/

#include <apr.h>
#include <apr_strings.h>
#include <httpd.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include "private.h"
#include "buffer.h"
#include "db.h"
#include "req.h"
#include "db_xml.h"
#include "xml_util.h"
#include "certs.h"
#include "db_ops.h"
#include "hashtable.h"
#include "acct_maint.h"
#include "foaf.h"


/**
 * generates a FOAF RDF document for the specified user and returns a pointer
 * to the xmlDoc. A NULL pointer is return on any error.
 */
xmlDocPtr
virgule_foaf_person (VirguleReq *vr, char *u)
{
  apr_pool_t *p = vr->r->pool;
  xmlDocPtr foaf, profile, staff;
  xmlNodePtr tree, ptree, tmpnode;
  char *db_key, *name, *url, *label;

  db_key = virgule_acct_dbkey (vr, u);
  if (db_key == NULL)
    return NULL;

  profile = virgule_db_xml_get (p, vr->db, db_key);
  if (profile == NULL)
    return NULL;

  ptree = virgule_xml_find_child (profile->xmlRootNode, "info");

  foaf = xmlNewDoc ((xmlChar *)"1.0");
  vr->r->content_type = "application/rdf+xml; charset=UTF-8";
  foaf->xmlRootNode = xmlNewDocNode (foaf, NULL, (xmlChar *)"rdf:RDF", NULL);
  xmlSetProp (foaf->xmlRootNode, (xmlChar *)"xmlns:rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
  xmlSetProp (foaf->xmlRootNode, (xmlChar *)"xmlns:rdfs", "http://www.w3.org/2000/01/rdf-schema#");
  xmlSetProp (foaf->xmlRootNode, (xmlChar *)"xmlns:foaf", "http://xmlns.com/foaf/0.1/");

  name = apr_pstrcat (p,
	       virgule_xml_get_prop (p, ptree, (xmlChar *)"givenname"), " ",
	       virgule_xml_get_prop (p, ptree, (xmlChar *)"surname"), NULL);

  tree = xmlNewChild (foaf->xmlRootNode, NULL, (xmlChar *)"foaf:PersonalProfileDocument", NULL);
  xmlSetProp (tree, (xmlChar *)"rdf:about", NULL);
  label = apr_pstrcat (p, vr->priv->site_name, " FOAF profile for ", name, NULL);
  xmlNewTextChild (tree, NULL, (xmlChar *)"rdfs:label", label);
  tmpnode = xmlNewChild (tree, NULL, (xmlChar *)"foaf:maker", NULL);
  xmlSetProp (tmpnode, (xmlChar *)"rdf:resource", "#me");
  tmpnode = xmlNewChild (tree, NULL, (xmlChar *)"foaf:primaryTopic", NULL);
  xmlSetProp (tmpnode, (xmlChar *)"rdf:resource", "#me");
  
  tree = xmlNewChild (foaf->xmlRootNode, NULL, (xmlChar *)"foaf:Person", NULL);
  xmlSetProp (tree, (xmlChar *)"rdf:about", "#me");
  xmlNewTextChild (tree, NULL, (xmlChar *)"foaf:name", (xmlChar *)name);
  xmlNewTextChild (tree, NULL, (xmlChar *)"foaf:nick", (xmlChar *)u);    

  url = virgule_xml_get_prop (p, ptree, (xmlChar *)"url");
  if (url == NULL)
    url = apr_pstrcat (p, vr->priv->base_uri, "/person/", ap_escape_uri(p, u), "/", NULL);
  tmpnode = xmlNewChild (tree, NULL, (xmlChar *)"foaf:homepage", NULL);
  xmlSetProp (tmpnode, (xmlChar *)"rdf:resource", (xmlChar *)url);

  url = apr_pstrcat (p, vr->priv->base_uri, "/person/", ap_escape_uri(p, u), "/diary.html", NULL);
  tmpnode = xmlNewChild (tree, NULL, (xmlChar *)"foaf:weblog", NULL);
  xmlSetProp (tmpnode, (xmlChar *)"rdf:resource", (xmlChar *)url);

  /* Convert outbound certs to foaf:knows properties */
  ptree = virgule_xml_find_child (profile->xmlRootNode, "certs");
  if (ptree)
    {
      xmlNodePtr cert, n1, n2;
      for (cert = ptree->children; cert != NULL; cert = cert->next)
	if (cert->type == XML_ELEMENT_NODE &&
	    !xmlStrcmp (cert->name, (xmlChar *)"cert"))
	  {
	    xmlChar *subject, *level;
	    subject = xmlGetProp (cert, (xmlChar *)"subj");
	    level = xmlGetProp (cert, (xmlChar *)"level");
	    if (xmlStrcmp (level, (xmlChar *)virgule_cert_level_to_name (vr, 0)))
	      {
                url = apr_pstrcat (p, vr->priv->base_uri, "/person/", ap_escape_uri(p, subject), "/foaf.rdf", NULL);
                n1 = xmlNewChild (tree, NULL, (xmlChar *)"foaf:knows", NULL);
		n1 = xmlNewChild (n1, NULL, (xmlChar *)"foaf:Person", NULL);
                xmlSetProp (n1, (xmlChar *)"rdf:about", apr_pstrcat(p, url, "#me", NULL));
                xmlNewTextChild (n1, NULL, (xmlChar *)"foaf:nick", (xmlChar *)subject);
                url = apr_pstrcat (p, vr->priv->base_uri, "/person/", ap_escape_uri(p, subject), "/foaf.rdf", NULL);
                n2 = xmlNewChild (n1, NULL, (xmlChar *)"foaf:seeAlso", NULL);
                xmlSetProp (n2, (xmlChar *)"rdf:resource", (xmlChar *)url);
	      }
	  }
    }

  /* Convert project staff associations to foaf:Project properties */
  db_key = apr_psprintf (p, "acct/%s/staff-person.xml", u);
  staff = virgule_db_xml_get (p, vr->db, db_key);
  if (staff != NULL)
    {
      xmlNodePtr n1;
      for (ptree = staff->xmlRootNode->children; ptree != NULL; ptree = ptree->next)
	{
	  char *name, *type;
	  name = virgule_xml_get_prop (p, ptree, (xmlChar *)"name");
	  type = virgule_xml_get_prop (p, ptree, (xmlChar *)"type");
	  if (! !strcmp (type, "None"))
	    {
	      n1 = xmlNewChild (tree, NULL, (xmlChar *)"foaf:currentProject", NULL);
              url = apr_pstrcat (p, vr->priv->base_uri, "/proj/", ap_escape_uri(p, name), "/", NULL);
              xmlSetProp (n1, (xmlChar *)"rdf:resource", (xmlChar *)url);
	    }
	}
    }

  return foaf;
}