<?php

/**
 * @file
 * Drush support for github.
 */

/**
 * Implementation of hook_drush_command().
 */
function github_drush_command() {
  $items = [];

  // Entity type commands
  $items['github-list'] = [
    'description' => 'List details of repositories',
    'options' => [
      'oauth' => 'Oauth token, if missing must be in ~/.drush/.github',
      'org' => 'Organization',
      'repos' => 'Repos name to match',
      'clone' => 'Flag to Clone the repos',
      'search' => 'Flag to Search (and clone) the repos',
    ],
    'examples' => [
      'github-repos-list' => 'List available repositories',
      'hublist --org=widget' => 'List available repositories for my organization named "widget"',
      'hublist --org=widget --repos="_profile$"' => 'List available repositories for my organization named "widget" whose name matches "_profile$"',
      'hublist --repos="_profile$" --clone' => 'Clone my organizations repos whose name matches "_profile$"',
      'hublist --repos="_profile$" --search="_AH"' => 'Search my organizations matching repos for string "_AH"',
    ],
    'aliases' => ['hublist'],
    'bootstrap' => DRUSH_BOOTSTRAP_NONE,
  ];

  return $items;
}

/**
 * Drush callback function to list github repositories.
 */
function drush_github_list() {
  $home = getenv('HOME');

  $oauth = drush_get_option('oauth');
  if (!$oauth) {
    $oauth = trim(@file_get_contents("$home/.drush/.github"));
    if (!$oauth) {
      return drush_set_error(dt('Missing oauth'));
    }
  }

  $org = drush_get_option('org');
  if (!$org) {
    $orgs = _drush_github_get_api("user/orgs", $oauth);
    if (count($orgs) == 1) {
      $org = $orgs[0]['login'];
    }
    else {
      $options = array_map(function ($org) {
        return $org['login'];
      }, $orgs);
      $org = $options[drush_choice($options, 'Select your organization')];
    }
  }

  $repos = drush_get_option('repos');

  $json = _drush_github_get_api("orgs/$org/repos", $oauth);
  if ($json) {
    // Parse the results for the actions.
    $results = [];
    foreach ($json as $repo) {
      $name = $repo['name'];
      if (!$repos || preg_match("/$repos/", $name)) {
        $results[$name] = str_replace('https://github.com/', 'git@github.com:', $repo['clone_url']);
      }
    }

    // Perform actions.
    $clone = drush_get_option('clone');
    $search = drush_get_option('search');
    if ($clone || $search) {
      // Get the cache directory.
      $cache_dir = drush_directory_cache("github/$org");

      // For each matching repo, handle the action.
      foreach ($results as $repo_name => $clone_url) {
        // Clone or pull the latest code.
        $cache_repo = "$cache_dir/$repo_name";
        if (is_dir($cache_repo)) {
          if (!_drush_github_shell_exec('cd %s; git pull', $cache_repo)) {
            drush_set_error(dt('Cannot git pull %repo', ['%repo' => $cache_repo]));
            continue;
          }
        }
        else {
          if (!_drush_github_shell_exec('cd %s; git clone %s %s', $cache_dir, $clone_url, $repo_name)) {
            drush_set_error(dt('Cannot git clone %url', ['%url' => $clone_url]));
            continue;
          }
        }

        // Search the repo.
        if ($search) {
          _drush_github_shell_exec('cd %s; grep --exclude-dir .git -r "%s" %s', $cache_dir, $search, $repo_name, ['print' => TRUE]);
        }
      }
    }
    else {
      drush_print_r($results);
    }
  }
}

/**
 * Get the github API JSON results and return as an array. Handle pagination.
 *
 * @param string $url
 *   The github API partial url.
 * @param string $oauth
 *   The github user oauth token.
 *
 * @return array
 *   The JSON results.
 */
function _drush_github_get_api($url, $oauth) {
  $results = [];
  $page = 1;
  $last = NULL;
  while (TRUE) {
    // Get the next page.
    $ch = curl_init();
    curl_setopt_array($ch, [
      CURLOPT_AUTOREFERER => TRUE,
      CURLOPT_FOLLOWLOCATION => TRUE,
      CURLOPT_HEADER => TRUE,
      CURLOPT_HTTPHEADER => [
        "Authorization: token $oauth",
        'Accept: application/json',
      ],
      CURLOPT_RETURNTRANSFER => TRUE,
      CURLOPT_URL => "https://api.github.com/$url?page=$page",
      CURLOPT_USERAGENT => 'Drush github (http://github.com/douggreen/drush_github)',
    ]);
    $response = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
    curl_close($ch);
    if ($code != 200) {
      return drush_set_error(dt('Invalid http response %code from github', ['%code' => $code]));
    }

    // Parse the response into the headers and body.
    list($headers, $body) = explode("\r\n\r\n", $response);

    // Combine the body results with the existing results.
    $result = json_decode($body, TRUE);
    $results = array_merge($results, $result);

    // Determine the last page.
    if ($last === NULL) {
      // We don't need to parse the headers. Just look for the last page in the
      // header response. It should look something like this:
      // @code
      //   Link: <https://api.github.com/organizations/123456789/repos?page=2>; rel="next", <https://api.github.com/organizations/123456789/repos?page=4>; rel="last"
      // @endcode
      if (preg_match_all('/page=(\d+)>; rel="last"/', $headers, $matches)) {
        $last = $matches[1][0];
      }
    }

    // Quit on the last page.
    if ($last === NULL || ++$page >= $last) {
      break;
    }

    // Avoid throttling.
    usleep(1000);
  }

  return $results;
}

/**
 * Handle the command output.
 *
 * @param $cmd
 *   The command to execute. May include placeholders used for sprintf.
 * @param ...
 *   Values for the placeholders specified in $cmd. Each of these will be passed through escapeshellarg() to ensure they are safe to use on the command line.
 * @param $options
 *   If the last argument is an array, then it is a set of options
 *   - print: always print results.
 *
 * @return int
 *   The shell exit code.
 */
function _drush_github_shell_exec() {
  // Get the arguments.
  $args = func_get_args();
  $options = is_array(end($args)) ? array_pop($args) : [];

  // Execute the command.
  $exit_code = _drush_shell_exec($args);

  // Get the output.
  $output = drush_shell_exec_output();

  // print the output.
  $print = isset($options['print']);
  $verbose = drush_get_context('DRUSH_VERBOSE');
  if (($print || $verbose) && $output) {
    $label = $print ? '' : 'Output: ';
    drush_print($label . implode("\n$label", $output));
  }

  return $exit_code;
}
