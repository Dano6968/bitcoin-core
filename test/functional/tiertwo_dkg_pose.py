#!/usr/bin/env python3
# Copyright (c) 2021 The PIVX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test deterministic masternodes"""

import time

from test_framework.test_framework import PivxDMNTestFramework
from test_framework.util import (
    assert_equal,
    wait_until,
)


class DkgPoseTest(PivxDMNTestFramework):

    def set_test_params(self):
        self.set_base_test_params()
        self.extra_args = [["-nuparams=v5_shield:1", "-nuparams=v6_evo:130", "-debug=llmq", "-debug=dkg", "-debug=net"]] * self.num_nodes
        self.extra_args[0].append("-sporkkey=932HEevBSujW2ud7RfB1YF91AFygbBRQj3de3LyaCRqNzKKgWXi")

    def disable_network_for_node(self, node):
        node.setnetworkactive(False)
        wait_until(lambda: node.getconnectioncount() == 0)

    def run_test(self):
        miner = self.nodes[self.minerPos]

        # initialize and start masternodes
        self.setup_test()
        assert_equal(len(self.mns), 6)

        # Mine a LLMQ final commitment regularly with 3 signers
        _, bad_mnode = self.mine_quorum()
        assert_equal(171, miner.getblockcount())
        assert bad_mnode is None

        # Mine the next final commitment after disconnecting a member
        _, bad_mnode = self.mine_quorum(invalidate_func=self.disable_network_for_node,
                                        skip_bad_member_sync=True)
        assert_equal(191, miner.getblockcount())
        assert bad_mnode is not None

        # Check PoSe
        self.log.info("Check that node %d has been PoSe punished..." % bad_mnode.idx)
        expected_penalty = 66
        assert_equal(expected_penalty, miner.listmasternodes(bad_mnode.proTx)[0]["dmnstate"]["PoSePenalty"])
        self.log.info("Node punished.")
        # penalty decreases at every block
        miner.generate(1)
        self.sync_all([n for i, n in enumerate(self.nodes) if i != bad_mnode.idx])
        expected_penalty -= 1
        assert_equal(expected_penalty, miner.listmasternodes(bad_mnode.proTx)[0]["dmnstate"]["PoSePenalty"])

        # Keep mining commitments until the bad node is banned
        timeout = time.time() + 600
        while time.time() < timeout:
            self.mine_quorum(invalidated_idx=bad_mnode.idx, skip_bad_member_sync=True)
            pose_height = miner.listmasternodes(bad_mnode.proTx)[0]["dmnstate"]["PoSeBanHeight"]
            if pose_height != -1:
                self.log.info("Node %d has been PoSeBanned at height %d" % (bad_mnode.idx, pose_height))
                self.log.info("All good.")
                return
            time.sleep(1)
        # timeout
        raise Exception("Node not banned after 10 minutes")



if __name__ == '__main__':
    DkgPoseTest().main()
