#!/bin/bash

# Git Worktree Helper Script
# Provides functions to manage git worktrees with new branches

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_success() {
    local msg="$1"
    echo -e "${GREEN}✓ ${msg}${NC}"
    return 0
}
print_error() {
    local msg="$1"
    echo -e "${RED}✗ ${msg}${NC}"
    return 0
}
print_warning() {
    local msg="$1"
    echo -e "${YELLOW}⚠ ${msg}${NC}"
    return 0
}
print_info() {
    local msg="$1"
    echo -e "${BLUE}ℹ ${msg}${NC}"
    return 0
}

# Copy environment files to worktree
copy_env_files() {
    local worktree_path="$1"
    local skip_env="$2"

    # Skip if --no-env flag was used
    if [[ "$skip_env" == "true" ]]; then
        print_info "Skipping environment file copy (--no-env flag used)"
        return 0
    fi

    # Copy local.env if it exists
    if [[ -f "local.env" ]]; then
        print_info "Copying local.env to worktree..."
        cp "local.env" "$worktree_path/local.env"

        # Ensure proper permissions (readable by owner only)
        chmod 600 "$worktree_path/local.env"

        print_success "Environment file copied successfully"
        print_warning "Sensitive data copied - ensure proper handling of credentials"
    else
        print_warning "local.env not found - skipping environment file copy"
    fi
}

# Show usage information
show_usage() {
    echo "Git Worktree Helper"
    echo ""
    echo "Usage:"
    echo "  $0 add <branch-name> [worktree-path] [--no-env]  Add worktree with new branch from current branch"
    echo "  $0 remove <worktree-path> [--force]              Remove worktree and optionally delete branch"
    echo "  $0 list                                          List all worktrees"
    echo "  $0 help                                          Show this help"
    echo ""
    echo "Options:"
    echo "  --no-env    Skip copying local.env file to the new worktree (for add)"
    echo "  --force     Force removal of worktree even if it has modified/untracked files (for remove)"
    echo ""
    echo "Examples:"
    echo "  $0 add feature-login                    # Creates worktree in ../feature-login with local.env"
    echo "  $0 add feature-login /tmp/feature-login # Creates worktree in /tmp/feature-login with local.env"
    echo "  $0 add feature-login --no-env           # Creates worktree without copying local.env"
    echo "  $0 remove ../feature-login              # Removes worktree and prompts to delete branch"
    echo "  $0 remove ../feature-login --force      # Force removes worktree and prompts to delete branch"
    echo ""
    return 0
}

# Validate we're in a git repository
check_git_repo() {
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        print_error "Not in a git repository"
        exit 1
    fi
    return 0
}

# Add worktree with new branch from current branch
add_worktree() {
    local branch_name="$1"
    local worktree_path="$2"
    local skip_env="$3"

    if [[ -z "$branch_name" ]]; then
        print_error "Branch name is required"
        show_usage
        exit 1
    fi

    # Default worktree path if not provided
    if [[ -z "$worktree_path" ]]; then
        worktree_path="../$branch_name"
    fi

    # Check if branch already exists
    if git show-ref --verify --quiet "refs/heads/$branch_name"; then
        print_error "Branch '$branch_name' already exists"
        print_info "Use 'git branch -D $branch_name' to delete it first, or choose a different name"
        exit 1
    fi

    # Check if worktree path already exists
    if [[ -e "$worktree_path" ]]; then
        print_error "Path '$worktree_path' already exists"
        exit 1
    fi

    local current_branch=$(git branch --show-current)
    print_info "Creating branch '$branch_name' from current branch '$current_branch'"
    print_info "Setting up worktree at '$worktree_path'"

    # Create worktree with new branch
    git worktree add -b "$branch_name" "$worktree_path"

    print_success "Worktree created successfully!"
    print_info "Branch: $branch_name"
    print_info "Path: $(realpath "$worktree_path")"

    # Copy environment files
    copy_env_files "$worktree_path" "$skip_env"

    print_info "To switch to the worktree: cd $worktree_path"
    return 0
}

# Remove worktree and optionally delete branch
remove_worktree() {
    local worktree_path="$1"
    local force="$2"

    if [[ -z "$worktree_path" ]]; then
        print_error "Worktree path is required"
        show_usage
        exit 1
    fi

    # Check if worktree exists
    if ! git worktree list | grep -q "$worktree_path"; then
        print_error "Worktree '$worktree_path' not found"
        print_info "Available worktrees:"
        git worktree list
        exit 1
    fi

    # Get the branch name associated with this worktree
    local branch_name=$(git worktree list | grep "$worktree_path" | awk '{print $3}' | tr -d '[]')

    print_info "Removing worktree at '$worktree_path'"
    print_info "Associated branch: '$branch_name'"

    if [[ "$force" == "true" ]]; then
        print_warning "Using --force: Any modified or untracked files will be discarded!"
        git worktree remove --force "$worktree_path"
    else
        git worktree remove "$worktree_path"
    fi

    print_success "Worktree removed successfully"

    # Ask if user wants to delete the branch
    if [[ -n "$branch_name" && "$branch_name" != "(detached" ]]; then
        echo ""
        print_warning "The branch '$branch_name' still exists."
        read -p "Do you want to delete the branch '$branch_name'? (y/N): " -n 1 -r
        echo ""

        if [[ $REPLY =~ ^[Yy]$ ]]; then
            git branch -D "$branch_name"
            print_success "Branch '$branch_name' deleted"
        else
            print_info "Branch '$branch_name' kept"
        fi
    fi
    return 0
}

# List all worktrees
list_worktrees() {
    print_info "Current worktrees:"
    git worktree list
    return 0
}

# Parse arguments for add command
parse_add_args() {
    local branch_name="$1"
    local arg2="$2"
    local arg3="$3"
    local worktree_path=""
    local skip_env="false"

    # Check if arg2 is --no-env flag
    if [[ "$arg2" == "--no-env" ]]; then
        skip_env="true"
    # Check if arg3 is --no-env flag
    elif [[ "$arg3" == "--no-env" ]]; then
        worktree_path="$arg2"
        skip_env="true"
    else
        worktree_path="$arg2"
    fi

    add_worktree "$branch_name" "$worktree_path" "$skip_env"
    return 0
}

# Parse arguments for remove command
parse_remove_args() {
    local worktree_path="$1"
    local arg2="$2"
    local force="false"

    if [[ "$arg2" == "--force" ]]; then
        force="true"
    elif [[ -n "$arg2" ]]; then
        print_error "Unknown option: $arg2"
        show_usage
        exit 1
    fi

    remove_worktree "$worktree_path" "$force"
    return 0
}

# Main script logic
main() {
    local cmd="$1"
    local arg2="$2"
    local arg3="$3"
    local arg4="$4"
    check_git_repo

    case "${cmd:-}" in
        "add")
            parse_add_args "$arg2" "$arg3" "$arg4"
            ;;
        "remove")
            parse_remove_args "$arg2" "$arg3"
            ;;
        "list")
            list_worktrees
            ;;
        "help"|"--help"|"-h")
            show_usage
            ;;
        "")
            print_error "No command provided"
            show_usage
            exit 1
            ;;
        *)
            print_error "Unknown command: $cmd"
            show_usage
            exit 1
            ;;
    esac
    return 0
}

# Run main function with all arguments
main "$@"