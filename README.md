# Drush github

Drush github command for listing, cloning, and searching github (organization) repositories.

If you are a member of only one organization, it will always use that.
You can specify your organization on the command line with ```--org=myorg```.
If you are a member of multiple organizations, and one is not specified, you will be prompted to select one.

You must have a github oauth token to use this drush command.
See https://help.github.com/articles/creating-a-personal-access-token-for-the-command-line/ for help creating a token.
The token can be placed in a file ~/.drush/.github or specified on the command line with ```--oauth=TOKEN```.

Use ```drush help github``` for all usage options.

# Examples

Search for all references to AH_ on my organization's repositories named *_profile, ignoring errors.

```
drush github-list --org=pfizer --repos='_profile$' --search=AH_ 2>/dev/null
```

List all my organizations repositories

```
drush github-list
```

List my organizations repositories named *-8, as an associative array whose value is the clone url.

```
drush github-list --repos='-8$'
```

Clone all my organizations repositories, displaying errors.

```
drush github-list --clone
```

# See also

* https://github.com/github/hub
