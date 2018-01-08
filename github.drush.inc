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
  $items['github'] = [
    'description' => 'List details of repositories',
    'options' => [
      'oauth' => 'Oauth token, if missing must be in ~/.drush/.github',
      'org' => 'Organization',
      'repos' => 'Repos name to match',
      'branch' => 'The branch',
      'list' => 'List the fields (this is the default action list=description, use list=all for all fields)',
      'clone' => 'Flag to Clone the repos',
      'search' => 'Flag to Search (and clone) the repos',
    ],
    'examples' => [
      'github' => 'List available repositories',
      'github --org=widget' => 'List available repositories for my organization named "widget"',
      'github --org=widget --repos="_profile$"' => 'List available repositories for my organization named "widget" whose name matches "_profile$"',
      'github --repos="_profile$" --clone' => 'Clone my organizations repos whose name matches "_profile$"',
      'github --repos="_profile$" --search="_AH"' => 'Search my organizations matching repos for string "_AH"',
    ],
    'bootstrap' => DRUSH_BOOTSTRAP_NONE,
    'engines' => [
      'outputformat' => [
        'default' => 'json',
        'pipe-format' => 'json',
      ],
    ],

  ];

  return $items;
}

/**
 * Drush callback function to list github repositories.
 */
function drush_github() {
  $oauth = drush_get_option('oauth');
  if (!$oauth) {
    $home = getenv('HOME');
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
    // Filter the repos.
    $results = [];
    foreach ($json as $repo) {
      $name = $repo['name'];
      if (!$repos || preg_match("/$repos/", $name)) {
        $results[] = $repo;
      }
    }

    // Perform actions.
    $clone = drush_get_option('clone');
    $search = drush_get_option('search');
    $branch = drush_get_option('branch');
    $list = drush_get_option('list');
    if (!$list && ($clone || $search)) {
      $output = _drush_github_clone($results, $org, $branch, $search);
    }
    else {
      if (!$list) {
        $list = ['description'];
      }
      elseif ($list != 'all') {
        $list = explode(',', $list);
      }

      $output = _drush_github($results, $oauth, $list, $search);
    }

    return $output;
  }
}

/**
 * Clone and or search the repo.
 *
 * @param array $json
 *   The API results.
 * @param string $org
 *   The organization.
 * @param string $branch
 *   The branch.
 * @param string $search
 *   String to search for.
 */
function _drush_github_clone($json, $org, $branch, $search) {
  $output = [];

  // Get the cache directory.
  $cache_dir = drush_directory_cache("github/$org");

  // For each matching repo, handle the action.
  foreach ($json as $repo) {
    $repo_name = $repo['name'];
    $clone_url = str_replace('https://github.com/', 'git@github.com:', $repo['clone_url']);

    // Clone the latest code (if it's not already cloned).
    $cache_repo = "$cache_dir/$repo_name";
    if (!is_dir($cache_repo) && !_drush_github_shell_exec('cd %s; git clone %s %s', $cache_dir, $clone_url, $repo_name)) {
      drush_print(dt('%repo: Cannot git clone %url', ['%url' => $clone_url, '%repo' => $repo_name]), 0, STDERR);
      continue;
    }

    // Checkout the branch.
    $default_branch = $branch ? $branch : $repo['default_branch'];
    if (!_drush_github_shell_exec('cd %s; git checkout %s', $cache_repo, $default_branch) && !_drush_github_shell_exec('cd %s; git checkout -b %s origin/%s', $cache_repo, $default_branch, $default_branch)) {
      drush_print(dt('%repo: Cannot git checkout %branch', ['%repo' => $repo_name, '%branch' => $default_branch]), 0, STDERR);
      continue;
    }
    if (!_drush_github_shell_exec('cd %s; git pull origin %s', $cache_repo, $default_branch)) {
      drush_print(dt('%repo: Cannot git pull origin %branch', ['%repo' => $repo_name, '%branch' => $default_branch]), 0, STDERR);
      continue;
    }

    // Search the repo.
    if ($search) {
      _drush_github_shell_exec('cd %s; grep --exclude-dir .git -r "%s" %s', $cache_dir, $search, $repo_name);

      // Save the results.
      $search_output = drush_shell_exec_output();
      if ($search_output) {
        $output[$repo_name] = $search_output;
      }
    }
  }

  return $output;
}

/**
 * List the results.
 *
 * @param array $json
 *   The API results.
 * @param string $oauth
 *   The Oauth token.
 * @param array $list
 *   List of field names.
 * @param string $search
 *   String to search for.
 */
function _drush_github($json, $oauth, $list, $search) {
  $output = [];

  // Parse the results for the actions.
  foreach ($json as $repo) {
    $name = $repo['name'];
    $result = [];

    $field_names = is_array($list) ? $list : array_keys($repo);
    foreach ($field_names as $field_name) {
      $value = $repo[$field_name];
      if ($field_name == 'clone_url') {
        $value = str_replace('https://github.com/', 'git@github.com:', $value);
      }
      $result[$field_name] = $value;
    }

    if (count($result) == 1) {
      // Get the result key and value.
      $key = reset($list);
      $value = reset($result);

      // If the result is a single value URL, retrieve it too.
      if (substr($key,-4) == '_url' && strpos($value, 'https://api.github.com/') !== FALSE) {
        $url_values = _drush_github_url($value, $oauth);
        if ($search) {
          foreach ($url_values as $url_key => $url_value) {
            if (!preg_match("/$search/", $url_key)) {
              unset($url_values[$url_key]);
            }
          }
          if (!$url_values) {
            continue;
          }
        }
        $value = [
          $value => $url_values,
        ];
      }

      // Save the single result.
      $output[$name] = $value;
    }
    else {
      // Save the multiple results.
      $output[$name] = $result;
    }
  }

  return $output;
}

/**
 * @param string $url
 *   The API call to make.
 * @param string $oauth
 *   The Oauth token.
 *
 * @return array
 *   The results retrieved from the API call.
 */
function _drush_github_url($url, $oauth) {
  // Strip existing https://api.github.com/ from the URL.
  $url = str_replace('https://api.github.com/', '', $url);

  // Strip optional pattern replacements such as {branch} from the URL.
  $url = preg_replace('/\{[^}]*\}/', '', $url);

  // Retrieve the URL from the API.
  $json = _drush_github_get_api($url, $oauth);

  // Format the results.
  $values = [];
  foreach ($json as $result) {
    if (!empty($result['name'])) {
      $name = $result['name'];
      $values[$name] = $result;
    }
    else {
      $values[] = $result;
    }
    $values[$name] = $result;
  }

  // Return the values.
  return $values;
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
 *
 * @return int
 *   The shell exit code.
 */
function _drush_github_shell_exec() {
  // Get the arguments.
  $args = func_get_args();

  // Execute the command.
  $exit_code = _drush_shell_exec($args);

  // Get the output.
  $output = drush_shell_exec_output();

  // print the output.
  $verbose = drush_get_context('DRUSH_VERBOSE');
  if ($verbose && $output) {
    $label = 'Output: ';
    drush_print($label . implode("\n$label", $output));
  }

  return $exit_code;
}
